// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "muse_tts.h"

/*
 * esp-sr's Chinese voice, for muse_tts_register(), or NULL with no voice data
 * installed. Maps the voice_data partition, a flash MMU change that freezes
 * the caches: call it from a task with its stack in internal RAM, as
 * muse_gadget_platform_start() runs. The voice loads on first use.
 */
const muse_tts_t *tts_mosaico_start(void);
