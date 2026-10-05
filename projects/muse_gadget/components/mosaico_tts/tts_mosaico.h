// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "muse_tts.h"

/*
 * Muse's voice, for muse_tts_register(): the speech server in
 * CONFIG_MOSAICO_TTS_URL, with esp-sr's Chinese voice when it's away; NULL
 * with neither. Maps the voice_data partition, a flash MMU change that freezes
 * the caches: call it from a task with its stack in internal RAM, as
 * muse_gadget_platform_start() runs. esp-sr's voice loads on first use.
 */
const muse_tts_t *tts_mosaico_start(void);
