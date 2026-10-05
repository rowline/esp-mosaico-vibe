// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sdkconfig.h"

#if CONFIG_MOSAICO_TTS_BENCH
/* Registers the bench RPCs (tts_bench.c) with ESP-Iris. */
void tts_bench_register(void);
#else
static inline void tts_bench_register(void) {}
#endif
