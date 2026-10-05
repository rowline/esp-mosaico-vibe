// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Presence greeting with the ESP-Mosaico Interaction module: someone walking
 * up lights its LEDs, wakes Muse's screen and, after a while away, gets a
 * greeting. Starts one task that owns the module; call once, from
 * muse_gadget_platform_start(). The task waits for Muse's boot animation and
 * for a module in either slot, so it can run before both.
 */
esp_err_t mosaico_presence_start(void);

/*
 * The module as Home Link's presence.read reports it. Without a module (none
 * claimed yet) only `module` is meaningful.
 */
typedef struct {
    bool module;                /* an Interaction module is claimed */
    bool someone;               /* motion within the last minute */
    int64_t last_motion_us;     /* esp_timer time of the last motion; 0: none since boot */
    int light;                  /* the module's light level, 0-100 relative; -1: not read yet */
} mosaico_presence_status_t;
void mosaico_presence_status(mosaico_presence_status_t *out);

/*
 * Lights the module's six LEDs one colour for `ms` (0: until changed), or
 * with r, g and b all zero turns them off. The arrival welcome leaves a colour
 * set here alone. Done by the task that owns the module: ESP_ERR_INVALID_STATE
 * without one, ESP_ERR_TIMEOUT if it didn't get to it, else the driver's
 * result.
 */
esp_err_t mosaico_presence_lights(uint8_t r, uint8_t g, uint8_t b, uint32_t ms);

/* Sends one NEC infrared code from the module's IR LED; errors as above. */
esp_err_t mosaico_presence_ir_send_nec(uint8_t address, uint8_t command);

#ifdef __cplusplus
}
#endif
