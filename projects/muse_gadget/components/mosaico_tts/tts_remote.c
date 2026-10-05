// SPDX-License-Identifier: Apache-2.0
/*
 * One message from the speech server: POST the text, read the PCM stream as
 * it's synthesized, resample it to 16 kHz and hand it over through a stream
 * buffer. The fetch runs on a task of its own, so the chat session never waits
 * on the network, with its stack in PSRAM (it touches no flash).
 *
 * Wi-Fi's receive buffers come out of internal DMA-capable RAM, of which the
 * board has about 37 KB to spare. Streamed as fast as it's synthesized, about
 * four times faster than speech at 24 kHz (185 KB/s), speech ran that down to
 * under 1 KB and crashed the board (2026-10-04). So the server is asked for
 * 16 kHz, paced at PACE times speech after its first second (about 50 KB/s),
 * and the socket is drained into a PSRAM buffer that a paced message never
 * fills; held back, the unread stream would sit in lwIP pinning those buffers.
 *
 * The task and the reader share the request; whichever lets go last frees it.
 */
#include "tts_remote.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "tts_remote";

#define OUT_RATE 16000
#define CONNECT_MS 1500         /* LAN: a server that's there answers at once */
#define READ_MS 5000            /* a stall mid-stream */
#define BUF_BYTES (OUT_RATE * 2 * 40)   /* 40 s ahead, 1.3 MB of PSRAM */
#define PACE "1.5"
#define TASK_STACK 6144
#define TASK_PRIORITY 4

enum { RUNNING, DONE, FAILED };

struct tts_remote {
    StreamBufferHandle_t pcm;
    char *body;
    atomic_int refs;
    atomic_bool cancel;
    atomic_int state;
    atomic_bool said;
    int rate;                   /* the server's, from x-audio-sample-rate */
};

static void release(tts_remote_t *r)
{
    if (atomic_fetch_sub(&r->refs, 1) == 1) {
        vStreamBufferDeleteWithCaps(r->pcm);
        free(r->body);
        free(r);
    }
}

static esp_err_t on_event(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key, "x-audio-sample-rate")) {
        ((tts_remote_t *)e->user_data)->rate = atoi(e->header_value);
    }
    return ESP_OK;
}

/* Linear interpolation down to 16 kHz, as Muse resamples its MP3 replies. */
typedef struct {
    uint32_t step;              /* Q16 input samples per output sample */
    uint32_t pos;               /* Q16, into prev followed by the next input */
    int16_t prev;
} down_t;

static size_t resample(down_t *d, const int16_t *in, size_t n, int16_t *out)
{
    size_t m = 0;
    if (d->step == 1u << 16) {
        memcpy(out, in, n * sizeof(int16_t));   /* the server already sends 16 kHz */
        return n;
    }
    while (d->pos < (n << 16)) {
        size_t i = d->pos >> 16;
        int32_t a = i ? in[i - 1] : d->prev, b = in[i];
        out[m++] = (int16_t)(a + (((b - a) * (int32_t)(d->pos & 0xFFFF)) >> 16));
        d->pos += d->step;
    }
    d->pos -= n << 16;
    d->prev = in[n - 1];
    return m;
}

/* Hands samples over as there's room, unless the reader has let go. */
static bool hand_over(tts_remote_t *r, const int16_t *s, size_t n)
{
    const uint8_t *p = (const uint8_t *)s;
    size_t left = n * sizeof(int16_t);
    while (left && !atomic_load(&r->cancel)) {
        size_t sent = xStreamBufferSend(r->pcm, p, left, pdMS_TO_TICKS(50));
        p += sent;
        left -= sent;
    }
    return !left;
}

static void fetch_task(void *arg)
{
    tts_remote_t *r = arg;
    int64_t t0 = esp_timer_get_time();
    esp_http_client_config_t cfg = {
        .url = CONFIG_MOSAICO_TTS_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONNECT_MS,
        .event_handler = on_event,
        .user_data = r,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    int16_t *in = malloc(1024 * sizeof(int16_t));
    int16_t *out = malloc(1024 * sizeof(int16_t));
    bool ok = false;
    size_t samples = 0;
    int status = 0;
    esp_err_t err = ESP_ERR_NO_MEM;
    if (http && in && out) {
        size_t len = strlen(r->body);
        esp_http_client_set_header(http, "Content-Type", "application/json");
        err = esp_http_client_open(http, (int)len);
    }
    if (err == ESP_OK && esp_http_client_write(http, r->body, (int)strlen(r->body)) < 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(http);
        status = esp_http_client_get_status_code(http);
        esp_http_client_set_timeout_ms(http, READ_MS);
    }
    if (err == ESP_OK && status == 200) {
        down_t down = { .step = (uint32_t)(((uint64_t)(r->rate > 0 ? r->rate : 24000) << 16) / OUT_RATE),
                        .pos = 1 << 16 };
        uint8_t *bytes = (uint8_t *)in;
        size_t have = 0;    /* bytes in `in`, an odd one left from the last read */
        for (;;) {
            if (atomic_load(&r->cancel)) {
                break;
            }
            int got = esp_http_client_read(http, (char *)bytes + have, 1024 * sizeof(int16_t) - have);
            if (got < 0) {
                ESP_LOGW(TAG, "stream broke after %u samples", (unsigned)samples);
                break;
            }
            if (got == 0) {
                ok = esp_http_client_is_complete_data_received(http);
                break;
            }
            have += got;
            size_t n = have / 2;
            if (n) {
                size_t m = resample(&down, in, n, out);
                samples += m;
                atomic_store(&r->said, true);
                if (!hand_over(r, out, m)) {
                    break;
                }
                if (have & 1) {
                    bytes[0] = bytes[have - 1];
                }
                have &= 1;
            }
        }
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "server answered %d", status);
    } else {
        ESP_LOGW(TAG, "server not reached: %s", esp_err_to_name(err));
    }
    if (http) {
        esp_http_client_cleanup(http);
    }
    free(in);
    free(out);
    if (ok) {
        ESP_LOGI(TAG, "%.1f s of speech fetched in %.1f s", samples / (double)OUT_RATE,
                 (esp_timer_get_time() - t0) / 1e6);
    }
    atomic_store(&r->state, ok ? DONE : FAILED);
    release(r);
    vTaskDeleteWithCaps(NULL);
}

/* text as a JSON string, quotes included, into out (cap bytes); false if it won't fit. */
static bool json_string(const char *text, char *out, size_t cap)
{
    size_t o = 0;
    out[o++] = '"';
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        char esc[8];
        const char *s = esc;
        if (*p == '"' || *p == '\\') {
            esc[0] = '\\', esc[1] = (char)*p, esc[2] = '\0';
        } else if (*p < 0x20) {
            snprintf(esc, sizeof(esc), "\\u%04x", *p);
        } else {
            esc[0] = (char)*p, esc[1] = '\0';
        }
        size_t n = strlen(s);
        if (o + n + 2 > cap) {
            return false;
        }
        memcpy(out + o, s, n);
        o += n;
    }
    out[o++] = '"';
    out[o] = '\0';
    return true;
}

tts_remote_t *tts_remote_start(const char *text, const char *language)
{
    tts_remote_t *r = calloc(1, sizeof(*r));
    if (!r) {
        return NULL;
    }
    size_t cap = strlen(text) * 6 + 160;
    char *quoted = malloc(cap);
    r->body = malloc(cap + 96);
    r->pcm = xStreamBufferCreateWithCaps(BUF_BYTES, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!quoted || !r->body || !r->pcm || !json_string(text, quoted, cap)) {
        goto fail;
    }
    snprintf(r->body, cap + 96,
             "{\"input\":%s,\"voice\":\"%s\",\"language\":\"%s\",\"response_format\":\"pcm\","
             "\"sample_rate\":%d,\"pace\":" PACE "}",
             quoted, CONFIG_MOSAICO_TTS_VOICE, language, OUT_RATE);
    free(quoted);
    quoted = NULL;
    atomic_init(&r->refs, 2);
    atomic_init(&r->state, RUNNING);
    if (xTaskCreateWithCaps(fetch_task, "tts_fetch", TASK_STACK, r, TASK_PRIORITY, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        goto fail;
    }
    return r;
fail:
    free(quoted);
    if (r->pcm) {
        vStreamBufferDeleteWithCaps(r->pcm);
    }
    free(r->body);
    free(r);
    return NULL;
}

int tts_remote_read(tts_remote_t *r, int16_t *pcm, size_t cap)
{
    int state = atomic_load(&r->state);   /* before draining: the task sets it last */
    size_t bytes = xStreamBufferBytesAvailable(r->pcm) & ~(size_t)1;   /* whole samples */
    if (bytes > cap * sizeof(int16_t)) {
        bytes = cap * sizeof(int16_t);
    }
    size_t got = bytes ? xStreamBufferReceive(r->pcm, pcm, bytes, 0) / sizeof(int16_t) : 0;
    if (got) {
        return (int)got;
    }
    return state == RUNNING ? TTS_REMOTE_LATER : state == DONE ? TTS_REMOTE_DONE : TTS_REMOTE_FAILED;
}

bool tts_remote_said(const tts_remote_t *r)
{
    return atomic_load(&r->said);
}

void tts_remote_stop(tts_remote_t *r)
{
    atomic_store(&r->cancel, true);
    release(r);
}
