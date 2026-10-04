// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct game_t *game_handle_t;
typedef struct {
    uint16_t width;
    uint16_t height;
} game_config_t;

typedef enum {
    GAME_DIR_UP = 0,
    GAME_DIR_RIGHT,
    GAME_DIR_DOWN,
    GAME_DIR_LEFT,
} game_dir_t;

typedef enum {
    GAME_PHASE_READY = 0, /* Snake placed, waiting for the first input. */
    GAME_PHASE_PLAYING,
    GAME_PHASE_OVER,      /* Hit a wall or itself. */
    GAME_PHASE_WON,       /* Filled the whole board. */
} game_phase_t;

typedef struct {
    uint32_t tick;
    int32_t pointer_x;
    int32_t pointer_y;
    bool pointer_down;
    bool paused;
    game_phase_t phase;
    game_dir_t dir;
    uint16_t score;
    uint16_t best;
    uint16_t length;
    uint8_t head_x;
    uint8_t head_y;
    uint8_t food_x;
    uint8_t food_y;
} game_snapshot_t;

/* Portable C shared by Host and device. One loop owns each instance and calls
 * these methods; input adapters enqueue or deliver events on that same loop. */
bool game_create(const game_config_t *config, game_handle_t *ret_handle);
void game_delete(game_handle_t handle);
/* Back to READY with tick and random sequence restarted; best score is kept. */
void game_reset(game_handle_t handle);
void game_set_paused(game_handle_t handle, bool paused);
/* Swipes steer; a tap starts, restarts, pauses (score bar) or turns toward it. */
void game_set_pointer(game_handle_t handle, int32_t x, int32_t y, bool pressed);
/* Absolute direction request from keys or buttons; reversals are ignored. */
void game_steer(game_handle_t handle, game_dir_t dir);
void game_update(game_handle_t handle);
bool game_render(game_handle_t handle);
game_snapshot_t game_read(game_handle_t handle);
/* Covers every gameplay field, including each body segment. */
uint32_t game_state_hash(game_handle_t handle);
