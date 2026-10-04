// SPDX-License-Identifier: Apache-2.0
#pragma once

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

#ifdef __cplusplus
}
#endif
