// SPDX-License-Identifier: Apache-2.0
/*
 * Muse board for ESP-Mosaico (ESP32-S31): 480 px square CO5300 AMOLED over
 * QSPI, CST9217 touch, one ES8311 for speaker and microphone, BQ27220 fuel
 * gauge, AI key (GPIO7, talk) and BOOT key (GPIO61, aux).
 *
 * The ESP-Mosaico BSP supplies pins for both board revisions, the power rails
 * and the Vibe Mode boot-splash handoff, so the screen keeps the splash until
 * Muse's first frame. LVGL and the flush follow Muse's Waveshare 1.75C board:
 * muse_lcd_bands, the panel's byte order, raw 0x51/0x10/0x11 commands. The
 * BSP's own LVGL layer is off (CONFIG_BSP_DISPLAY_LVGL_ENABLE=n).
 *
 * Holding AI through any reset, including a deep-sleep wake, boots Vibe Mode,
 * so power-off asks the board to cut power instead of sleeping until AI.
 *
 * Captions and replies show Chinese from fonts/cjk_16.bin, GB2312's hanzi and
 * punctuation in Source Han Sans at unscii_16's 16 px.
 */
#include <stdatomic.h>

#include "bsp/esp_mosaico.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "boards/muse_lcd_bands.h"
#include "muse_board.h"
#include "muse_mem.h"

static const char *TAG = "board";

#define DRAW_BUF_LINES 120      /* four bands to the screen (muse_lcd_bands.h) */
#define LCD_CHUNK_BYTES (BSP_LCD_H_RES * 8 * 2)
#define ES8311_GAIN_STEP_DB 6
#define BQ27220_STATUS_DSG (1u << 0)    /* BatteryStatus: discharging */
#define SHUTDOWN_WAIT_MS 1000
#define GAUGE_TASK_STACK 4096

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_touch_handle_t s_tp;
static muse_gpio_button_t s_ai;
static muse_gpio_button_t s_boot;
static atomic_bool s_gauge;     /* set once by gauge_task */

/* bsp_battery_init() writes its profile into the BQ27220 when the gauge's
 * copy differs, which took about 8 s on first boot. Do it beside the UI;
 * read_power() reports no battery until it's done. */
static void gauge_task(void *arg)
{
    (void)arg;
    esp_err_t err = bsp_battery_init();
    if (err == ESP_OK) {
        atomic_store(&s_gauge, true);
    } else {
        ESP_LOGW(TAG, "fuel gauge unavailable (%s): battery status disabled", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

static esp_err_t init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init");
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_ai, BSP_BUTTON_AI_GPIO), TAG, "AI key");
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_boot, BSP_BUTTON_BOOT_GPIO), TAG, "BOOT key");
    if (xTaskCreate(gauge_task, "gauge", GAUGE_TASK_STACK, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGW(TAG, "no memory for the fuel gauge task: battery status disabled");
    }
    return ESP_OK;
}

/* The CO5300 here takes windows 4 px aligned across and 2 px down, as the
 * BSP's own LVGL rounder sends them. */
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~3;
    a->x2 |= 3;
    a->y1 &= ~1;
    a->y2 |= 1;
}

/* Runs on the pinned muse_boot task, so bsp_display_new() sets up the QSPI bus
 * and its interrupt on MUSE_UI_CORE, as muse_lcd_bands needs. */
static lv_display_t *display_start(lv_indev_t **touch)
{
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = MUSE_UI_CORE;
    adapter_cfg.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        return NULL;
    }

    esp_lcd_panel_handle_t panel;
    const bsp_display_config_t panel_cfg = BSP_DISPLAY_DEFAULT_CONFIG();
    if (bsp_display_new(&panel_cfg, &panel) != ESP_OK) {
        return NULL;
    }
    s_io = bsp_display_get_panel_io();
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = panel,
        .panel_io = s_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = muse_lcd_bands_register(disp_cfg, DRAW_BUF_LINES, LCD_CHUNK_BYTES);
    if (!disp) {
        return NULL;
    }
    lv_display_add_event_cb(disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);

    /* The adapter reads the CST9217 on its INT line (GPIO6), not by polling. */
    if (bsp_touch_new(BSP_DISPLAY_ROTATE_0, &s_tp) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_touch_config_t tp_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, s_tp);
    *touch = esp_lv_adapter_register_touch(&tp_cfg);
    if (!*touch || esp_lv_adapter_start() != ESP_OK) {
        return NULL;
    }
    return disp;
}

static bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

static void send_brightness(void *level)
{
    /* CO5300 "write display brightness" (0x51), as the BSP sends it. */
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (0x51 << 8), level, 1);
}

static void set_brightness(int pct)
{
    uint8_t level = (uint8_t)(pct * 255 / 100);
    muse_lcd_bands_run(send_brightness, &level);
}

static void send_sleep(void *sleep)
{
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | ((*(bool *)sleep ? 0x10 : 0x11) << 8), NULL, 0);
}

/* Plain SLPIN/SLPOUT. bsp_display_off()/on() would use deep standby, whose
 * wake reruns the init table, and after the splash handoff that table has no
 * Sleep Out. */
static void panel_sleep(bool sleep)
{
    muse_lcd_bands_run(send_sleep, &sleep);
    vTaskDelay(pdMS_TO_TICKS(120));   /* settle before the next command */
}

/* LVGL stops; touch stays awake. The CST9217 has no reset line here to wake it
 * from deep sleep. */
static void display_pause(bool pause)
{
    if (pause) {
        esp_lv_adapter_pause(-1);
    } else {
        esp_lv_adapter_resume();
    }
}

/* One I2S bus for both; Muse opens them at the same rate (16 kHz). */
static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    *spk = bsp_audio_codec_speaker_init();
    *mic = bsp_audio_codec_microphone_init();
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    /* ES8311 PGA steps are 6 dB; snap so the UI shows what's applied. */
    db = (db / ES8311_GAIN_STEP_DB) * ES8311_GAIN_STEP_DB;
    esp_codec_dev_set_in_gain(mic, (float)db);
}

static unsigned poll_buttons(void)
{
    return muse_gpio_button_poll(&s_ai) | muse_gpio_button_poll(&s_boot) << 2;   /* BOOT is aux */
}

static esp_err_t read_power(muse_power_t *out)
{
    if (!atomic_load(&s_gauge)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    bsp_battery_status_t st;
    ESP_RETURN_ON_ERROR(bsp_battery_read(&st), TAG, "fuel gauge read");
    out->battery_pct = st.state_of_charge > 100 ? 100 : st.state_of_charge;
    out->battery_mv = st.voltage_mv > 0 ? st.voltage_mv : 0;
    out->usb = !(st.status_flags & BQ27220_STATUS_DSG);
    out->charging = out->usb && st.current_ma > 0;
    return ESP_OK;
}

/* GPIO57 asks the board to cut power. On USB the board stays up, so this
 * releases the request and reports the failure Muse shows on screen. */
static esp_err_t power_off(void)
{
    ESP_LOGI(TAG, "requesting board shutdown");
    ESP_RETURN_ON_ERROR(bsp_power_set_shutdown(true), TAG, "shutdown request");
    vTaskDelay(pdMS_TO_TICKS(SHUTDOWN_WAIT_MS));
    bsp_power_set_shutdown(false);
    ESP_LOGW(TAG, "still powered after %d ms; on USB power?", SHUTDOWN_WAIT_MS);
    return ESP_ERR_INVALID_STATE;
}

/* fonts/cjk_16.bin, in flash. LVGL's loader copies it into PSRAM
 * (muse_lv_mem.c): about 0.9 MB, once. */
extern const uint8_t cjk_16_start[] asm("_binary_cjk_16_bin_start");
extern const uint8_t cjk_16_end[] asm("_binary_cjk_16_bin_end");

static const lv_font_t *wide_font(void)
{
    int64_t t0 = esp_timer_get_time();
    lv_font_t *font = lv_binfont_create_from_buffer((void *)cjk_16_start, cjk_16_end - cjk_16_start);
    if (!font) {
        ESP_LOGW(TAG, "wide font didn't load: no Chinese on screen");
        return NULL;
    }
    ESP_LOGI(TAG, "wide font loaded in %d ms", (int)((esp_timer_get_time() - t0) / 1000));
    return font;
}

static const muse_board_t s_board = {
    .name = "ESP-Mosaico",
    .width = BSP_LCD_H_RES,
    .height = BSP_LCD_V_RES,
    .round = false,
    .touch = true,
    .diagonal_in = 1.8f,
    .talk_button = "AI",
    .aux_button = "BOOT",
    /* AI is on the top edge, right of centre; BOOT is on the bottom edge,
     * left of the USB-C port (ON/OFF is to its right). Clear of the rounded
     * corners, the status line and the page dots. */
    .talk_hint = { LV_ALIGN_TOP_MID, 140, 14 },
    .aux_hint = { LV_ALIGN_BOTTOM_MID, -110, -12 },
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .display_pause = display_pause,
    .wide_font = wide_font,
    .audio_init = audio_init,
    .mic_slot = 0,
    .set_mic_gain = set_mic_gain,
    .poll_buttons = poll_buttons,
    .read_power = read_power,
    .power_off = power_off,
};

/* CONFIG_MUSE_BOARD_HOST: Muse's app_main starts with this board. */
const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
