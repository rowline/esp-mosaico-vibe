// SPDX-License-Identifier: Apache-2.0
#pragma once

#define GAME_NAME "snake"
enum { GAME_WIDTH = 480, GAME_HEIGHT = 480, GAME_TICK_HZ = 30 };
/* Host simulator action codes: arrows/WASD send left, right, up (as "jump")
 * and down (as "back"); P pauses and Enter restarts. */
enum {
    GAME_ACTION_LEFT = 0,
    GAME_ACTION_RIGHT = 1,
    GAME_ACTION_UP = 2,
    GAME_ACTION_PAUSE = 3,
    GAME_ACTION_RESET = 4,
    GAME_ACTION_DOWN = 5,
    GAME_ACTION_COUNT = 7,
};
