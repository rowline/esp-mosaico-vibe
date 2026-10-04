// SPDX-License-Identifier: Apache-2.0
/*
 * Presence greeting with the ESP-Mosaico Interaction module (PIR and six
 * WS2812s) in either expansion slot.
 *
 * Motion after LEAVE_AFTER_MS without any is an arrival: Muse's screen wakes
 * and the LEDs fade up to warm white for WELCOME_MS. If nobody was seen for
 * GREET_GAP_MS before it, Muse also greets for those WELCOME_MS: the happy
 * animation, a chirp, and a caption for the local time of day, which goes out
 * with the lights. Motion while someone is there keeps the screen awake but
 * neither wakes it nor relights the LEDs, so a screen put to sleep with BOOT
 * stays dark until they leave and come back. LEAVE_AFTER_MS without motion
 * counts as gone; Muse's own auto-sleep darkens the screen.
 *
 * One task owns the module handle and this state. It waits for Muse to leave
 * its boot animation, then claims the module, waiting while none is plugged
 * in. Pulling the module out while it runs isn't noticed: its LEDs and PIR
 * just stop until the next restart.
 */
#include "mosaico_presence.h"

#include <stdint.h>
#include <stdio.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "mosaico_module_interact.h"
#include "muse_state.h"
#include "muse_ui.h"
#include "muse_voice.h"
#include "presence_clock.h"

static const char *TAG = "presence";

#define TASK_STACK 4096
#define TASK_PRIORITY 3
#define BOOT_POLL_MS 500
#define CLAIM_WAIT_MS 5000          /* each wait for a module to be plugged in */
#define POLL_MS 200                 /* the PIR holds its output for seconds */
#define LEAVE_AFTER_MS 60000
#define GREET_GAP_MS (10 * 60 * 1000)
#define WELCOME_MS 6000             /* lights, and the greeting with them */
#define SCREEN_WAKE_MS 1000         /* longest wait for the panel before greeting */
#define SCREEN_POLL_MS 20
#define FADE_IN_MS 400
#define FADE_OUT_MS 600
#define FADE_STEP_MS 20
#define LED_BRIGHTNESS 96           /* the driver's scale for every colour, of 255 */
#define LEVEL_FULL 255

static const mosaico_interact_rgb_t WARM_WHITE = { 255, 150, 60 };

typedef struct {
    mosaico_interact_handle_t module;
    bool present;
    int64_t last_motion_us;         /* 0: nobody seen since boot */
    int64_t welcome_until_us;       /* 0: lights off */
    bool greeting;                  /* the greeting caption is up */
    uint32_t greeting_version;      /* caption version the greeting set */
    char before[MUSE_CAPTION_MAX];  /* caption the greeting covered */
} presence_t;

/* In PSRAM, like the task's stack: internal RAM is short with Muse running. */
EXT_RAM_BSS_ATTR static presence_t s_presence;

static int64_t ms_to_us(int64_t ms)
{
    return ms * 1000;
}

static void wait_for_muse(void)
{
    while (muse_state_mode(NULL) == MUSE_MODE_BOOT) {
        vTaskDelay(pdMS_TO_TICKS(BOOT_POLL_MS));
    }
}

/* NULL if opening failed for a reason other than no module. */
static mosaico_interact_handle_t open_module(void)
{
    mosaico_interact_config_t config = MOSAICO_INTERACT_DEFAULT_CONFIG();
    config.discovery_timeout_ms = CLAIM_WAIT_MS;
    config.button_mode = MOSAICO_INTERACT_BUTTON_MODE_GPIO;   /* buttons unused: touch sensor stays off */
    config.led_brightness = LED_BRIGHTNESS;
    bool told = false;
    for (;;) {
        mosaico_interact_handle_t module = NULL;
        esp_err_t err = mosaico_interact_open(&config, &module);
        if (err == ESP_OK) {
            return module;
        }
        if (err != ESP_ERR_TIMEOUT) {
            /* A non-NULL handle here still holds the slot: leave it claimed. */
            ESP_LOGE(TAG, "Interaction module didn't open (%s): no presence greeting", esp_err_to_name(err));
            return NULL;
        }
        if (!told) {
            ESP_LOGI(TAG, "waiting for an Interaction module in either slot");
            told = true;
        }
    }
}

static void fade(int from, int to, int ms)
{
    int steps = ms / FADE_STEP_MS;
    for (int i = 1; i <= steps; i++) {
        int level = from + (to - from) * i / steps;
        mosaico_interact_rgb_t colour = {
            .r = (uint8_t)(WARM_WHITE.r * level / LEVEL_FULL),
            .g = (uint8_t)(WARM_WHITE.g * level / LEVEL_FULL),
            .b = (uint8_t)(WARM_WHITE.b * level / LEVEL_FULL),
        };
        if (mosaico_interact_led_fill(s_presence.module, colour) != ESP_OK) {
            return;   /* the driver logged it; the next fade sends a whole frame */
        }
        vTaskDelay(pdMS_TO_TICKS(FADE_STEP_MS));
    }
}

static void format_greeting(char *out, size_t cap)
{
    const char *name = CONFIG_MOSAICO_PRESENCE_NAME;
    const char *comma = name[0] ? "，" : "";
    int hour;
    if (!presence_clock_hour(&hour)) {
        snprintf(out, cap, "你好%s%s！", comma, name);
    } else if (hour >= 23 || hour < 5) {
        snprintf(out, cap, "夜深了%s%s，早点休息", comma, name);
    } else {
        const char *hello = hour < 11 ? "早上好" : hour < 13 ? "中午好" : hour < 18 ? "下午好" : "晚上好";
        snprintf(out, cap, "%s%s%s！", hello, comma, name);
    }
}

/* The happy animation, a chirp and the caption, once the panel is lit. */
static void greet(void)
{
    for (int waited = 0; muse_ui_dark() && waited < SCREEN_WAKE_MS; waited += SCREEN_POLL_MS) {
        vTaskDelay(pdMS_TO_TICKS(SCREEN_POLL_MS));
    }
    uint32_t any = UINT32_MAX;   /* differs from the live version, so this copies */
    muse_state_caption(s_presence.before, sizeof(s_presence.before), &any);
    char text[96];
    format_greeting(text, sizeof(text));
    muse_state_set_caption("%s", text);
    char ignored[4];
    muse_state_caption(ignored, sizeof(ignored), &s_presence.greeting_version);
    muse_state_make_happy();
    muse_voice_request_chirp();
    s_presence.greeting = true;
    ESP_LOGI(TAG, "greeting: %s", text);
}

/* Lights out, and back to the caption the greeting covered unless something
 * has replaced it. */
static void end_welcome(void)
{
    s_presence.welcome_until_us = 0;
    if (s_presence.greeting) {
        s_presence.greeting = false;
        char ignored[4];
        bool replaced = muse_state_caption(ignored, sizeof(ignored), &s_presence.greeting_version);
        if (!replaced && muse_state_mode(NULL) == MUSE_MODE_IDLE) {
            muse_state_set_caption("%s", s_presence.before);
        }
    }
    fade(LEVEL_FULL, 0, FADE_OUT_MS);
    (void)mosaico_interact_led_clear(s_presence.module);
}

static void arrive(int64_t now)
{
    bool greeting = !s_presence.last_motion_us || now - s_presence.last_motion_us >= ms_to_us(GREET_GAP_MS);
    ESP_LOGI(TAG, "someone arrived%s", greeting ? "" : " (seen recently: no greeting)");
    s_presence.present = true;
    muse_state_set_asleep(false);   /* the input task resumes a paused display within a poll */
    fade(0, LEVEL_FULL, FADE_IN_MS);
    if (greeting && muse_state_mode(NULL) == MUSE_MODE_IDLE) {
        greet();
    }
    s_presence.welcome_until_us = esp_timer_get_time() + ms_to_us(WELCOME_MS);
}

static void leave(void)
{
    ESP_LOGI(TAG, "nobody around");
    s_presence.present = false;
}

static void presence_task(void *arg)
{
    (void)arg;
    wait_for_muse();
    s_presence.module = open_module();
    if (!s_presence.module) {
        vTaskDeleteWithCaps(NULL);
        return;
    }
    mosaico_interact_inputs_t inputs;
    /* Someone there at start-up saw the boot: no welcome. */
    if (mosaico_interact_read_inputs(s_presence.module, &inputs) == ESP_OK && inputs.motion_detected) {
        s_presence.present = true;
        s_presence.last_motion_us = esp_timer_get_time();
    }
    ESP_LOGI(TAG, "watching for people");
    for (;;) {
        presence_clock_poll();
        int64_t now = esp_timer_get_time();
        if (mosaico_interact_read_inputs(s_presence.module, &inputs) == ESP_OK) {
            if (inputs.motion_detected) {
                if (s_presence.present) {
                    muse_state_poke();   /* keeps the screen from auto-sleeping */
                } else {
                    arrive(now);
                }
                s_presence.last_motion_us = now;
            } else if (s_presence.present && now - s_presence.last_motion_us >= ms_to_us(LEAVE_AFTER_MS)) {
                leave();
            }
        }
        if (s_presence.welcome_until_us && esp_timer_get_time() >= s_presence.welcome_until_us) {
            end_welcome();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t mosaico_presence_start(void)
{
    /* Stack in PSRAM: the task never writes flash, so it never runs with the
     * cache off, and it hands the drivers no buffers on its stack. */
    if (xTaskCreateWithCaps(presence_task, "presence", TASK_STACK, NULL, TASK_PRIORITY, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
