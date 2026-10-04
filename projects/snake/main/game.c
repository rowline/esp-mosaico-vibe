// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include "game_internal.h"

static const int8_t DIR_DX[4] = {0, 1, 0, -1};
static const int8_t DIR_DY[4] = {-1, 0, 1, 0};
static const uint32_t RNG_SEED = 0x5eed1234U;

static uint16_t cell_index(game_cell_t cell)
{
    return (uint16_t)(cell.y * GAME_COLS + cell.x);
}

static game_dir_t opposite(game_dir_t dir)
{
    return (game_dir_t)((dir + 2) & 3);
}

static uint32_t next_random(struct game_t *game)
{
    uint32_t value = game->rng;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->rng = value;
    return value;
}

/* Picks uniformly among free cells; false when the snake fills the board. */
static bool spawn_food(struct game_t *game)
{
    uint16_t free_cells = (uint16_t)(GAME_CELLS - game->length);
    if (!free_cells) return false;
    uint32_t pick = next_random(game) % free_cells;
    for (uint16_t index = 0; index < GAME_CELLS; ++index) {
        if (game->occupied[index]) continue;
        if (pick-- == 0) {
            game->food = (game_cell_t){(uint8_t)(index % GAME_COLS),
                                       (uint8_t)(index / GAME_COLS)};
            return true;
        }
    }
    return false;
}

static void new_round(struct game_t *game)
{
    memset(game->occupied, 0, sizeof(game->occupied));
    game->head = 0;
    game->length = GAME_START_LENGTH;
    for (uint16_t i = 0; i < GAME_START_LENGTH; ++i) {
        game_cell_t cell = {(uint8_t)(GAME_START_X - i), GAME_START_Y};
        game->body[i] = cell;
        game->occupied[cell_index(cell)] = 1;
    }
    game->dir = GAME_DIR_RIGHT;
    game->turn_count = 0;
    game->score = 0;
    game->progress = 0;
    game->new_best = false;
    game->phase = GAME_PHASE_READY;
    (void)spawn_food(game);
}

static void start_play(struct game_t *game)
{
    game->phase = GAME_PHASE_PLAYING;
    /* Start timing varies later food between rounds yet stays replayable. */
    game->rng ^= game->tick * 2654435761U;
    if (!game->rng) game->rng = RNG_SEED;
}

static void queue_turn(struct game_t *game, game_dir_t dir)
{
    game_dir_t last = game->turn_count ? game->turns[game->turn_count - 1] : game->dir;
    if (dir == last || dir == opposite(last) || game->turn_count >= GAME_TURN_QUEUE) return;
    game->turns[game->turn_count++] = dir;
}

static void finish(struct game_t *game, game_phase_t phase)
{
    game->phase = phase;
    game->over_tick = game->tick;
    game->turn_count = 0;
    if (game->score > game->best) {
        game->best = game->score;
        game->new_best = true;
    }
}

static void step_snake(struct game_t *game)
{
    if (game->turn_count) {
        game->dir = game->turns[0];
        --game->turn_count;
        memmove(game->turns, game->turns + 1, game->turn_count * sizeof(game->turns[0]));
    }
    game_cell_t head = game_segment(game, 0);
    int next_x = head.x + DIR_DX[game->dir];
    int next_y = head.y + DIR_DY[game->dir];
    if (next_x < 0 || next_y < 0 || next_x >= GAME_COLS || next_y >= GAME_ROWS) {
        finish(game, GAME_PHASE_OVER);
        return;
    }
    game_cell_t next = {(uint8_t)next_x, (uint8_t)next_y};
    bool grow = next.x == game->food.x && next.y == game->food.y;
    uint16_t tail = cell_index(game_segment(game, (uint16_t)(game->length - 1)));
    uint16_t target = cell_index(next);
    /* The tail cell is free this step unless the snake is growing. */
    if (game->occupied[target] && (grow || target != tail)) {
        finish(game, GAME_PHASE_OVER);
        return;
    }
    if (grow) ++game->length;
    else game->occupied[tail] = 0;
    game->head = (uint16_t)((game->head + GAME_CELLS - 1) % GAME_CELLS);
    game->body[game->head] = next;
    game->occupied[target] = 1;
    if (!grow) return;
    ++game->score;
    if (!spawn_food(game)) finish(game, GAME_PHASE_WON);
}

static void handle_tap(struct game_t *game, int32_t x, int32_t y)
{
    if (game->paused) {
        game->paused = false;
        return;
    }
    switch (game->phase) {
    case GAME_PHASE_READY:
        start_play(game);
        break;
    case GAME_PHASE_PLAYING: {
        if (y < GAME_BOARD_Y) {
            game->paused = true;
            break;
        }
        /* Turn toward the tap, across the current line of travel. */
        game_dir_t heading = game->turn_count ? game->turns[game->turn_count - 1] : game->dir;
        game_cell_t head = game_segment(game, 0);
        int32_t dx = x - (GAME_BOARD_X + head.x * GAME_CELL_PX + GAME_CELL_PX / 2);
        int32_t dy = y - (GAME_BOARD_Y + head.y * GAME_CELL_PX + GAME_CELL_PX / 2);
        if (heading == GAME_DIR_LEFT || heading == GAME_DIR_RIGHT) {
            if (abs(dy) >= GAME_CELL_PX / 2) queue_turn(game, dy < 0 ? GAME_DIR_UP : GAME_DIR_DOWN);
        } else if (abs(dx) >= GAME_CELL_PX / 2) {
            queue_turn(game, dx < 0 ? GAME_DIR_LEFT : GAME_DIR_RIGHT);
        }
        break;
    }
    case GAME_PHASE_OVER:
    case GAME_PHASE_WON:
        if (!game_restart_ready(game)) break;
        new_round(game);
        start_play(game);
        break;
    }
}

bool game_create(const game_config_t *config, game_handle_t *ret_handle)
{
    if (!ret_handle) return false;
    *ret_handle = NULL;
    if (!config || !config->width || !config->height) return false;
    game_handle_t game = calloc(1, sizeof(*game));
    if (!game) return false;
    game->config = *config;
    game_reset(game);
    *ret_handle = game;
    return true;
}

void game_delete(game_handle_t game)
{
    free(game);
}

void game_reset(game_handle_t game)
{
    if (!game) return;
    game->tick = 0;
    game->over_tick = 0;
    game->rng = RNG_SEED;
    game->paused = false;
    game->pointer_down = false;
    game->swiped = false;
    new_round(game);
}

void game_set_paused(game_handle_t game, bool paused)
{
    if (game) game->paused = paused;
}

void game_steer(game_handle_t game, game_dir_t dir)
{
    if (!game || game->paused || dir > GAME_DIR_LEFT) return;
    switch (game->phase) {
    case GAME_PHASE_READY:
        start_play(game);
        break;
    case GAME_PHASE_PLAYING:
        break;
    case GAME_PHASE_OVER:
    case GAME_PHASE_WON:
        if (!game_restart_ready(game)) return;
        new_round(game);
        start_play(game);
        break;
    }
    queue_turn(game, dir);
}

void game_set_pointer(game_handle_t game, int32_t x, int32_t y, bool pressed)
{
    if (!game) return;
    x = x < 0 ? 0 : (x >= game->config.width ? game->config.width - 1 : x);
    y = y < 0 ? 0 : (y >= game->config.height ? game->config.height - 1 : y);
    if (pressed && !game->pointer_down) {
        game->anchor_x = x;
        game->anchor_y = y;
        game->swiped = false;
    } else if (pressed) {
        int32_t dx = x - game->anchor_x;
        int32_t dy = y - game->anchor_y;
        if (abs(dx) >= GAME_SWIPE_PX || abs(dy) >= GAME_SWIPE_PX) {
            /* Re-anchor so one continuous drag can chain several turns. */
            game_steer(game, abs(dx) > abs(dy) ? (dx > 0 ? GAME_DIR_RIGHT : GAME_DIR_LEFT)
                                               : (dy > 0 ? GAME_DIR_DOWN : GAME_DIR_UP));
            game->swiped = true;
            game->anchor_x = x;
            game->anchor_y = y;
        }
    } else if (game->pointer_down && !game->swiped) {
        handle_tap(game, x, y);
    }
    game->pointer_x = x;
    game->pointer_y = y;
    game->pointer_down = pressed;
}

void game_update(game_handle_t game)
{
    if (!game || game->paused) return;
    ++game->tick;
    if (game->phase != GAME_PHASE_PLAYING) return;
    uint32_t speed = GAME_SPEED_START + (uint32_t)game->score * GAME_SPEED_PER_FOOD;
    game->progress = (uint16_t)(game->progress + (speed > GAME_SPEED_MAX ? GAME_SPEED_MAX : speed));
    if (game->progress < GAME_STEP_COST) return;
    game->progress = (uint16_t)(game->progress - GAME_STEP_COST);
    step_snake(game);
}

game_snapshot_t game_read(game_handle_t game)
{
    if (!game) return (game_snapshot_t){0};
    game_cell_t head = game_segment(game, 0);
    return (game_snapshot_t){
        .tick = game->tick,
        .pointer_x = game->pointer_x,
        .pointer_y = game->pointer_y,
        .pointer_down = game->pointer_down,
        .paused = game->paused,
        .phase = game->phase,
        .dir = game->dir,
        .score = game->score,
        .best = game->best,
        .length = game->length,
        .head_x = head.x,
        .head_y = head.y,
        .food_x = game->food.x,
        .food_y = game->food.y,
    };
}

static uint32_t mix(uint32_t hash, uint32_t value)
{
    return (hash ^ value) * 16777619U;
}

uint32_t game_state_hash(game_handle_t game)
{
    if (!game) return 0;
    /* Hash named fields, avoiding platform-dependent struct padding. */
    uint32_t hash = 2166136261U;
    const uint32_t fields[] = {
        game->tick, game->phase, game->paused, game->dir, game->score, game->best,
        game->progress, game->rng, game->length, game->food.x, game->food.y,
        game->turn_count, (uint32_t)game->pointer_x, (uint32_t)game->pointer_y,
        game->pointer_down,
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) hash = mix(hash, fields[i]);
    for (uint16_t i = 0; i < game->length; ++i) hash = mix(hash, cell_index(game_segment(game, i)));
    return hash;
}
