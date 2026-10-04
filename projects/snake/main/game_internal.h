// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Private to game.c (rules and input) and game_view.c (drawing). */
#include "game.h"

enum {
    GAME_COLS = 20,
    GAME_ROWS = 18,
    GAME_CELLS = GAME_COLS * GAME_ROWS,
    GAME_CELL_PX = 22,
    GAME_BOARD_X = 20,
    GAME_BOARD_Y = 64,
    GAME_BOARD_W = GAME_COLS * GAME_CELL_PX,
    GAME_BOARD_H = GAME_ROWS * GAME_CELL_PX,
    GAME_START_LENGTH = 3,
    GAME_START_X = 6,
    GAME_START_Y = GAME_ROWS / 2,
    GAME_TURN_QUEUE = 3,
    /* Speed in thousandths of a cell per tick: 6 cells/s rising to 12.6 at 30 Hz. */
    GAME_SPEED_START = 200,
    GAME_SPEED_PER_FOOD = 8,
    GAME_SPEED_MAX = 420,
    GAME_STEP_COST = 1000,
    GAME_RESTART_DELAY_TICKS = 15,
    GAME_SWIPE_PX = 24,
};

typedef struct {
    uint8_t x;
    uint8_t y;
} game_cell_t;

struct game_t {
    game_config_t config;
    game_phase_t phase;
    bool paused;
    bool new_best;
    uint32_t tick;
    uint32_t over_tick;
    uint32_t rng;
    uint16_t score;
    uint16_t best;
    uint16_t progress;
    /* Ring buffer: body[head] is the head, later indices run toward the tail. */
    game_cell_t body[GAME_CELLS];
    uint16_t head;
    uint16_t length;
    uint8_t occupied[GAME_CELLS];
    game_cell_t food;
    game_dir_t dir;
    game_dir_t turns[GAME_TURN_QUEUE];
    uint8_t turn_count;
    int32_t pointer_x;
    int32_t pointer_y;
    bool pointer_down;
    bool swiped;
    int32_t anchor_x;
    int32_t anchor_y;
};

static inline game_cell_t game_segment(const struct game_t *game, uint16_t index)
{
    return game->body[(game->head + index) % GAME_CELLS];
}

static inline bool game_restart_ready(const struct game_t *game)
{
    return game->tick - game->over_tick >= GAME_RESTART_DELAY_TICKS;
}
