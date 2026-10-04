// SPDX-License-Identifier: Apache-2.0
/*
 * Muse camera backend (components/camera in the Muse SDK) for the ESP-Mosaico
 * camera module in the left expansion slot, with an OV3640 or SC101IOT sensor.
 *
 * Both sensors give YUV422 (UYVY) at their Kconfig default size; the SC101IOT
 * has no JPEG mode. A capture claims the module, lets auto-exposure settle,
 * takes one frame and tears everything down, so between captures the sensor
 * is unpowered and its buffers are freed. The frame is encoded to JPEG here,
 * turned upright: the sensor sits a quarter turn clockwise on the module, as
 * the BSP's mosaico_camera_jpeg_decode_rgb888_ccw90() corrects for AI models.
 *
 * The photo then shows on the screen for a few seconds, as sent.
 */
#include "camera_mosaico.h"

#include <inttypes.h>
#include <string.h>

#include "esp_cache.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mosaico_module_camera.h"
#include "muse_ui.h"

static const char *TAG = "camera";

#define FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define PIX_FMT_UYVY FOURCC('U', 'Y', 'V', 'Y')

#define MAX_WIDTH 1280          /* the SC101IOT's 720p; the OV3640 gives 1024 x 768 */
#define MAX_HEIGHT 1280         /* upright */
#define SETTLE_FRAMES 10        /* auto-exposure, under a second at either frame rate */
#define JPEG_QUALITY 80
#define SHOW_MS 4000            /* the photo on screen, then Muse's reply */

/* The frame's YUV422 as YUYV, packed rows: the order the JPEG encoder rotates. */
static uint8_t *to_yuyv(const mosaico_camera_frame_t *f)
{
    size_t row = (size_t)f->width * 2;
    size_t stride = f->bytes_per_line ? f->bytes_per_line : row;
    uint8_t *out = heap_caps_aligned_alloc(16, row * f->height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) {
        return NULL;
    }
    /* The camera wrote it by DMA. */
    esp_cache_msync((void *)f->data, f->size, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    for (uint32_t y = 0; y < f->height; y++) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)f->data + y * stride);
        uint32_t *dst = (uint32_t *)(out + y * row);
        for (size_t i = 0; i < row / 4; i++) {
            uint32_t w = src[i];   /* U Y0 V Y1, low byte first */
            dst[i] = (w & 0x00FF00FF) << 8 | (w >> 8 & 0x00FF00FF);
        }
    }
    return out;
}

static uint8_t clamp(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

static void hide_photo(void *arg)
{
    (void)arg;
    muse_ui_image_hide();
}

/*
 * The photo, upright and scaled to the screen's height, over a black screen
 * (muse_ui_image_draw()) until SHOW_MS have passed or a talk or tap hides it.
 */
static void show(const uint8_t *yuyv, int w, int h)
{
    static esp_timer_handle_t s_hide;
    int sw, sh;
    if (!muse_ui_image_size(&sw, &sh)) {
        return;
    }
    int out_w = sh * h / w, out_h = sh;     /* upright: h wide, w tall */
    if (out_w > sw) {
        out_h = sw * w / h;
        out_w = sw;
    }
    uint8_t *px = heap_caps_malloc((size_t)out_w * out_h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!px) {
        return;
    }
    uint8_t *p = px;
    for (int oy = 0; oy < out_h; oy++) {
        for (int ox = 0; ox < out_w; ox++) {
            /* A quarter turn anticlockwise: the source's right edge is the top. */
            int x = w - 1 - oy * w / out_h;
            int y = ox * h / out_w;
            const uint8_t *pair = yuyv + ((size_t)y * w + (x & ~1)) * 2;
            int c = 298 * (pair[x & 1 ? 2 : 0] - 16);
            int d = pair[1] - 128, e = pair[3] - 128;
            uint8_t r = clamp((c + 409 * e + 128) >> 8);
            uint8_t g = clamp((c - 100 * d - 208 * e + 128) >> 8);
            uint8_t b = clamp((c + 516 * d + 128) >> 8);
            uint16_t rgb = (r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3;
            *p++ = rgb >> 8;
            *p++ = rgb & 0xFF;
        }
    }
    bool shown = muse_ui_image_draw((sw - out_w) / 2, (sh - out_h) / 2, out_w, out_h, (const uint16_t *)px);
    heap_caps_free(px);
    if (!shown) {
        return;
    }
    if (!s_hide) {
        const esp_timer_create_args_t args = { .callback = hide_photo, .name = "camera_show" };
        if (esp_timer_create(&args, &s_hide) != ESP_OK) {
            return;
        }
    }
    esp_timer_stop(s_hide);
    esp_timer_start_once(s_hide, SHOW_MS * 1000);
}

static esp_err_t encode(const uint8_t *yuyv, int w, int h, camera_frame_t *out)
{
    jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
    cfg.width = w;
    cfg.height = h;
    cfg.src_type = JPEG_PIXEL_FORMAT_YCbYCr;
    cfg.subsampling = JPEG_SUBSAMPLE_420;
    cfg.quality = JPEG_QUALITY;
    cfg.rotate = JPEG_ROTATE_270D;          /* clockwise: a quarter turn anticlockwise */

    int cap = w * h / 2;
    uint8_t *jpeg = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg) {
        return ESP_ERR_NO_MEM;
    }
    jpeg_enc_handle_t enc = NULL;
    int len = 0;
    jpeg_error_t ret = jpeg_enc_open(&cfg, &enc);
    if (ret == JPEG_ERR_OK) {
        ret = jpeg_enc_process(enc, yuyv, w * h * 2, jpeg, cap, &len);
        jpeg_enc_close(enc);
    }
    if (ret != JPEG_ERR_OK || len <= 0) {
        ESP_LOGE(TAG, "JPEG encode failed: %d", ret);
        heap_caps_free(jpeg);
        return ESP_FAIL;
    }
    uint8_t *fit = heap_caps_realloc(jpeg, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    jpeg = fit ? fit : jpeg;
    *out = (camera_frame_t){
        .jpeg = jpeg,
        .len = (size_t)len,
        .width = h,
        .height = w,
        .priv = jpeg,
    };
    return ESP_OK;
}

static esp_err_t grab(mosaico_camera_handle_t cam, camera_frame_t *out)
{
    ESP_RETURN_ON_ERROR(mosaico_camera_open(cam), TAG, "open");
    ESP_RETURN_ON_ERROR(mosaico_camera_start_stream(cam), TAG, "start stream");
    ESP_RETURN_ON_ERROR(mosaico_camera_discard_frames(cam, SETTLE_FRAMES), TAG, "settle");

    mosaico_camera_frame_t frame;
    ESP_RETURN_ON_ERROR(mosaico_camera_get_frame(cam, &frame), TAG, "frame");
    uint8_t *yuyv = NULL;
    esp_err_t err = ESP_OK;
    if (frame.pixel_format != PIX_FMT_UYVY || frame.width % 16 || frame.height % 16
        || frame.width > MAX_WIDTH || frame.height > MAX_WIDTH) {
        ESP_LOGE(TAG, "unexpected frame: %" PRIu32 "x%" PRIu32 ", format 0x%08" PRIx32,
                 frame.width, frame.height, frame.pixel_format);
        err = ESP_ERR_INVALID_RESPONSE;
    } else if (!(yuyv = to_yuyv(&frame))) {
        err = ESP_ERR_NO_MEM;
    }
    int w = (int)frame.width, h = (int)frame.height;
    mosaico_camera_return_frame(cam, &frame);
    if (err == ESP_OK) {
        int64_t t0 = esp_timer_get_time();
        err = encode(yuyv, w, h, out);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "%dx%d frame to a %u-byte JPEG in %d ms", w, h, (unsigned)out->len,
                     (int)((esp_timer_get_time() - t0) / 1000));
            show(yuyv, w, h);
        }
    }
    heap_caps_free(yuyv);
    return err;
}

static esp_err_t capture(camera_frame_t *out)
{
    mosaico_camera_config_t cfg = MOSAICO_CAMERA_DEFAULT_CONFIG();   /* UYVY, the sensor's size */
    cfg.allow_unidentified = true;      /* as the BSP's camera examples */

    mosaico_camera_handle_t cam = NULL;
    esp_err_t err = mosaico_camera_new(&cfg, &cam);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no camera module in the left slot (%s)", esp_err_to_name(err));
        return err == ESP_ERR_TIMEOUT ? ESP_ERR_NOT_FOUND : err;
    }
    err = grab(cam, out);
    mosaico_camera_stop_stream(cam);
    mosaico_camera_close(cam);
    mosaico_camera_del(cam);
    return err;
}

static void release(camera_frame_t *frame)
{
    heap_caps_free(frame->priv);
}

static const camera_driver_t s_driver = {
    .name = "camera module on ESP-Mosaico",
    .max_width = MAX_WIDTH,
    .max_height = MAX_HEIGHT,
    .capture = capture,
    .release = release,
};

const camera_driver_t *camera_mosaico(void)
{
    return &s_driver;
}
