// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include "game.h"
#include "game_config.h"
#include "mosaico_game_module.h"
#include "mosaico_raylib_fast.h"

typedef struct {
    game_handle_t game;
    bool held[GAME_ACTION_COUNT];
    bool pointer_active;
    int32_t pointer_track;
} host_state_t;

static const char *const PHASE_NAMES[] = {"ready", "playing", "over", "won"};
static const char *const DIR_NAMES[] = {"up", "right", "down", "left"};

static int initialize(void *value, const char *asset_root)
{
    (void)asset_root;
    host_state_t *state = value;
    const game_config_t config = {GAME_WIDTH, GAME_HEIGHT};
    if (!game_create(&config, &state->game)) return -1;
    InitWindow(GAME_WIDTH, GAME_HEIGHT, GAME_NAME);
    SetTargetFPS(GAME_TICK_HZ);
    return 0;
}

static void shutdown(void *value)
{
    host_state_t *state = value;
    game_delete(state->game);
    state->game = NULL;
}

static bool steer_action(int32_t code, game_dir_t *dir)
{
    switch (code) {
    case GAME_ACTION_LEFT: *dir = GAME_DIR_LEFT; return true;
    case GAME_ACTION_RIGHT: *dir = GAME_DIR_RIGHT; return true;
    case GAME_ACTION_UP: *dir = GAME_DIR_UP; return true;
    case GAME_ACTION_DOWN: *dir = GAME_DIR_DOWN; return true;
    default: return false;
    }
}

static void action(host_state_t *state, int32_t code, bool pressed)
{
    if (code < 0 || code >= GAME_ACTION_COUNT) return;
    /* The runner repeats held left/right/up every tick; act on presses only. */
    bool rising = pressed && !state->held[code];
    state->held[code] = pressed;
    if (!rising) return;
    game_dir_t dir;
    if (steer_action(code, &dir)) {
        /* The simulator page also turns touches in its bottom strip into
         * left/right/up for platformers; touches already steer through
         * game_set_pointer(), so ignore those duplicates. */
        if (!game_read(state->game).pointer_down) game_steer(state->game, dir);
    } else if (code == GAME_ACTION_RESET) {
        game_reset(state->game);
    } else if (code == GAME_ACTION_PAUSE) {
        game_set_paused(state->game, !game_read(state->game).paused);
    }
}

/* Single touch: follow whichever pointer went down first until it lifts. The
 * browser page reports the mouse as track 1, replays as track 0. */
static void pointer(host_state_t *state, const mosaico_host_input_v1_t *event)
{
    if (!state->pointer_active && event->pressed) {
        state->pointer_active = true;
        state->pointer_track = event->track_id;
    }
    if (!state->pointer_active || event->track_id != state->pointer_track) return;
    game_set_pointer(state->game, event->x, event->y, event->pressed);
    if (!event->pressed) state->pointer_active = false;
}

static void input(void *value, const mosaico_host_input_v1_t *event)
{
    host_state_t *state = value;
    if (!event) return;
    if (event->type == MOSAICO_HOST_INPUT_POINTER) {
        pointer(state, event);
    } else if (event->type == MOSAICO_HOST_INPUT_CONTROL) {
        switch (event->code) {
        case MOSAICO_HOST_CONTROL_PAUSE: game_set_paused(state->game, true); break;
        case MOSAICO_HOST_CONTROL_RESUME: game_set_paused(state->game, false); break;
        case MOSAICO_HOST_CONTROL_RESET: game_reset(state->game); break;
        default: break;
        }
    } else if (event->type == MOSAICO_HOST_INPUT_ACTION) {
        action(state, event->code, event->pressed);
    }
}

static void update(void *value)
{
    game_update(((host_state_t *)value)->game);
}

static int render(void *value)
{
    return game_render(((host_state_t *)value)->game) ? 0 : -1;
}

static uint32_t state_hash(const void *value)
{
    return game_state_hash(((const host_state_t *)value)->game);
}

static int state_json(const void *value, char *output, size_t capacity)
{
    if (!output || !capacity) return -1;
    const game_snapshot_t state = game_read(((const host_state_t *)value)->game);
    int size = snprintf(output, capacity,
        "{\"phase\":\"%s\",\"tick\":%lu,\"score\":%u,\"best\":%u,\"length\":%u,"
        "\"dir\":\"%s\",\"head_x\":%u,\"head_y\":%u,\"food_x\":%u,\"food_y\":%u,"
        "\"pointer_x\":%ld,\"pointer_y\":%ld,\"pointer_down\":%s,\"state_hash\":\"%08lx\"}",
        state.paused ? "paused" : PHASE_NAMES[state.phase], (unsigned long)state.tick,
        state.score, state.best, state.length, DIR_NAMES[state.dir],
        state.head_x, state.head_y, state.food_x, state.food_y,
        (long)state.pointer_x, (long)state.pointer_y,
        state.pointer_down ? "true" : "false", (unsigned long)state_hash(value));
    return size < 0 || (size_t)size >= capacity ? -1 : size;
}

static const mosaico_game_module_v1_t s_module = {
    .descriptor = {MOSAICO_HOST_GAME_ABI_V1, GAME_NAME, GAME_NAME,
                   GAME_WIDTH, GAME_HEIGHT, GAME_TICK_HZ, 1},
    .state_size = sizeof(host_state_t),
    .initialize = initialize, .shutdown = shutdown, .input = input,
    .update = update, .render = render,
    .state_hash = state_hash, .state_json = state_json,
};

const mosaico_game_module_v1_t *mosaico_game_module_v1(void)
{
    return &s_module;
}
