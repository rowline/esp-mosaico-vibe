// SPDX-License-Identifier: Apache-2.0
/*
 * A bench for the speech server path, over ESP-Iris RPC, to reproduce on
 * demand what a spoken reply loads the board with (Wi-Fi streaming at several
 * times speech rate into PSRAM) without asking Muse anything:
 *
 *   python3 mosaico.py iris rpc --project projects/muse_gadget 29524 1 --payload "5"
 *   python3 mosaico.py iris rpc --project projects/muse_gadget 29524 1 --payload "3|Some text."
 *   python3 mosaico.py iris rpc --project projects/muse_gadget 29524 1 --payload "p2"
 *
 * The payload is a repeat count, optionally after "p" to play the speech as
 * well, and followed by "|" and the text; the default text is about 40 s of
 * English. Each fetch is drained as it arrives (or as it's played) and logged
 * with the heap's internal and DMA headroom.
 *
 * Method 2 hammers PSRAM data for a number of seconds from a task on each
 * core: static data (in the region that faulted on 2026-10-04/05, just past
 * the PSRAM copy of the code and constants) and small heap blocks, written,
 * copied and checked. Run it beside method 1 to load Wi-Fi and audio too.
 *
 *   python3 mosaico.py iris rpc --project projects/muse_gadget 29524 2 --payload "300"
 */
#include "tts_bench.h"

#if CONFIG_MOSAICO_TTS_BENCH

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_ipc.h"
#include "riscv/csr.h"
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "muse_audio.h"
#include "tts_remote.h"

static const char *TAG = "tts_bench";

#define SERVICE_ID 0x7354       /* "TS" */
#define FETCH_METHOD 1
#define HAMMER_METHOD 2
#define PMP_METHOD 3
#define HAMMER_WORDS 2048       /* 8 KB of static PSRAM data per core */
#define TEXT_MAX 1024

static const char DEFAULT_TEXT[] =
    "Here is a longer answer, to keep the speaker busy for a while. The board "
    "streams speech from the computer on the same network, several times "
    "faster than it is spoken, and keeps it in external memory until it is "
    "played. Meanwhile the screen keeps drawing, the microphone stays ready, "
    "and the chat connection to Muse stays open. If something in that mix "
    "goes wrong, this test should find it, so it runs the same request "
    "again and again, and reports how much internal memory is left each time.";

static bool s_running;

typedef struct {
    int repeats;
    bool play;
    char text[TEXT_MAX];
} bench_t;

static void bench_task(void *arg)
{
    bench_t *b = arg;
    static int16_t pcm[1024];
    for (int i = 0; i < b->repeats; i++) {
        int64_t t0 = esp_timer_get_time();
        tts_remote_t *r = tts_remote_start(b->text, "english");
        if (!r) {
            ESP_LOGE(TAG, "run %d: couldn't start", i + 1);
            break;
        }
        size_t samples = 0, low_internal = SIZE_MAX, low_dma = SIZE_MAX;
        int got;
        while ((got = tts_remote_read(r, pcm, 1024)) != TTS_REMOTE_DONE && got != TTS_REMOTE_FAILED) {
            if (got > 0) {
                samples += got;
                if (b->play) {
                    muse_audio_write(pcm, (size_t)got);
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t dma = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
            low_internal = internal < low_internal ? internal : low_internal;
            low_dma = dma < low_dma ? dma : low_dma;
        }
        tts_remote_stop(r);
        ESP_LOGI(TAG, "run %d/%d%s: %s, %.1f s of speech in %.1f s, lowest free %u internal, %u DMA",
                 i + 1, b->repeats, b->play ? " (played)" : "", got == TTS_REMOTE_DONE ? "done" : "failed", samples / 16000.0,
                 (esp_timer_get_time() - t0) / 1e6, (unsigned)low_internal, (unsigned)low_dma);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    free(b);
    s_running = false;
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t fetch_rpc(const esp_iris_rpc_request_t *request, uint8_t *response,
                           size_t response_capacity, size_t *response_size, void *user_ctx)
{
    (void)user_ctx;
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    bench_t *b = calloc(1, sizeof(*b));
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    char arg[TEXT_MAX + 8];
    size_t n = request->payload_size < sizeof(arg) - 1 ? request->payload_size : sizeof(arg) - 1;
    memcpy(arg, request->payload, n);
    arg[n] = '\0';
    b->play = arg[0] == 'p';
    b->repeats = atoi(arg + b->play) > 0 ? atoi(arg + b->play) : 1;
    const char *bar = strchr(arg, '|');
    strlcpy(b->text, bar && bar[1] ? bar + 1 : DEFAULT_TEXT, sizeof(b->text));
    s_running = true;
    if (xTaskCreateWithCaps(bench_task, "tts_bench", 4096, b, 3, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_running = false;
        free(b);
        return ESP_ERR_NO_MEM;
    }
    int w = snprintf((char *)response, response_capacity, "{\"started\":%d}", b->repeats);
    *response_size = w > 0 && (size_t)w < response_capacity ? (size_t)w : 0;
    return ESP_OK;
}

EXT_RAM_BSS_ATTR static uint32_t s_hammer_data[2][HAMMER_WORDS];
static volatile int s_hammering;

static void hammer_task(void *arg)
{
    int core = (int)(intptr_t)arg >> 16;
    int seconds = (int)(intptr_t)arg & 0xFFFF;
    uint32_t *data = s_hammer_data[core];
    int64_t end = esp_timer_get_time() + (int64_t)seconds * 1000000;
    uint32_t rounds = 0, bad = 0, seed = 0x9E3779B9u * (core + 1);
    while (esp_timer_get_time() < end) {
        for (int i = 0; i < HAMMER_WORDS; i++) {
            data[i] = seed ^ (uint32_t)i;
        }
        uint32_t *block = heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (block) {
            memcpy(block, data, 256);
            for (int i = 0; i < 64; i++) {
                bad += block[i] != (seed ^ (uint32_t)i);
            }
            heap_caps_free(block);
        }
        for (int i = 0; i < HAMMER_WORDS; i++) {
            bad += data[i] != (seed ^ (uint32_t)i);
        }
        seed = seed * 1664525u + 1013904223u;
        if (++rounds % 2000 == 0) {
            vTaskDelay(1);   /* let the idle task feed the watchdog */
        }
    }
    ESP_LOGI(TAG, "hammer on core %d: %" PRIu32 " rounds, %" PRIu32 " bad words", core, rounds, bad);
    s_hammering--;
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t hammer_rpc(const esp_iris_rpc_request_t *request, uint8_t *response,
                            size_t response_capacity, size_t *response_size, void *user_ctx)
{
    (void)user_ctx;
    if (s_hammering) {
        return ESP_ERR_INVALID_STATE;
    }
    char arg[16];
    size_t n = request->payload_size < sizeof(arg) - 1 ? request->payload_size : sizeof(arg) - 1;
    memcpy(arg, request->payload, n);
    arg[n] = '\0';
    int seconds = atoi(arg) > 0 && atoi(arg) < 3600 ? atoi(arg) : 60;
    for (int core = 0; core < 2; core++) {
        if (xTaskCreatePinnedToCoreWithCaps(hammer_task, "hammer", 3072, (void *)(intptr_t)(core << 16 | seconds),
                                            1, NULL, core, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) == pdPASS) {
            s_hammering++;
        }
    }
    int w = snprintf((char *)response, response_capacity, "{\"hammering\":%d,\"seconds\":%d}", s_hammering, seconds);
    *response_size = w > 0 && (size_t)w < response_capacity ? (size_t)w : 0;
    ESP_LOGI(TAG, "hammering PSRAM data at %p and %p for %d s", s_hammer_data[0], s_hammer_data[1], seconds);
    return ESP_OK;
}

/* Method 3: each core's PMP and PMA registers, to compare what the two cores
 * enforce. One line per core: cfg0..3, then addr0..15. */
typedef struct {
    uint32_t cfg[4], addr[16];
} pmp_regs_t;

static void read_pmp(void *arg)
{
    pmp_regs_t *r = arg;
    r->cfg[0] = RV_READ_CSR(CSR_PMPCFG0);
    r->cfg[1] = RV_READ_CSR(CSR_PMPCFG0 + 1);
    r->cfg[2] = RV_READ_CSR(CSR_PMPCFG0 + 2);
    r->cfg[3] = RV_READ_CSR(CSR_PMPCFG0 + 3);
#define A(i) r->addr[i] = RV_READ_CSR(CSR_PMPADDR0 + i)
    A(0); A(1); A(2); A(3); A(4); A(5); A(6); A(7);
    A(8); A(9); A(10); A(11); A(12); A(13); A(14); A(15);
#undef A
}

static esp_err_t pmp_rpc(const esp_iris_rpc_request_t *request, uint8_t *response,
                         size_t response_capacity, size_t *response_size, void *user_ctx)
{
    (void)request;
    (void)user_ctx;
    pmp_regs_t regs[2];
    size_t o = 0;
    for (int core = 0; core < 2; core++) {
        esp_err_t err = esp_ipc_call_blocking(core, read_pmp, &regs[core]);
        if (err != ESP_OK) {
            return err;
        }
        int w = snprintf((char *)response + o, response_capacity - o, "core%d cfg", core);
        o += w > 0 ? w : 0;
        for (int i = 0; i < 4 && o < response_capacity; i++) {
            o += snprintf((char *)response + o, response_capacity - o, " %08" PRIx32, regs[core].cfg[i]);
        }
        o += snprintf((char *)response + o, response_capacity - o, " addr<<2");
        for (int i = 0; i < 16 && o < response_capacity; i++) {
            o += snprintf((char *)response + o, response_capacity - o, " %08" PRIx32, regs[core].addr[i] << 2);
        }
        o += snprintf((char *)response + o, response_capacity - o, "\n");
    }
    *response_size = o < response_capacity ? o : response_capacity;
    return ESP_OK;
}

void tts_bench_register(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_rpc_register(SERVICE_ID, PMP_METHOD, pmp_rpc, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_rpc_register(SERVICE_ID, FETCH_METHOD, fetch_rpc, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_rpc_register(SERVICE_ID, HAMMER_METHOD, hammer_rpc, NULL));
}

#endif /* CONFIG_MOSAICO_TTS_BENCH */
