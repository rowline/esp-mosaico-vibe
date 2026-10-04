// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

/*
 * Local time for the greeting. Home Link doesn't set the clock, so the
 * presence task calls presence_clock_poll(), which starts SNTP the first time
 * Wi-Fi is up. Both run in the presence task only.
 */
void presence_clock_poll(void);

/* The local hour (0-23); false until the clock has been set. */
bool presence_clock_hour(int *hour);
