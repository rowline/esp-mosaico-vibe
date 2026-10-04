// SPDX-License-Identifier: Apache-2.0
#include "game_config.h"
#include "game_internal.h"
#include "mosaico_raylib_fast.h"

#define RGB(r, g, b) {(r), (g), (b), 255}

static const Color COLOR_BACKGROUND = RGB(10, 14, 22);
static const Color COLOR_BORDER = RGB(48, 66, 86);
static const Color COLOR_TILE_DARK = RGB(22, 32, 44);
static const Color COLOR_TILE_LIGHT = RGB(26, 38, 52);
static const Color COLOR_LABEL = RGB(120, 150, 180);
static const Color COLOR_VALUE = RGB(235, 240, 245);
static const Color COLOR_HEAD = RGB(150, 235, 120);
static const Color COLOR_HEAD_DEAD = RGB(235, 90, 80);
static const Color COLOR_BODY_NEAR = RGB(100, 210, 110);
static const Color COLOR_BODY_FAR = RGB(36, 120, 80);
static const Color COLOR_EYE = RGB(240, 250, 240);
static const Color COLOR_PUPIL = RGB(16, 24, 20);
static const Color COLOR_FOOD = RGB(235, 72, 72);
static const Color COLOR_FOOD_SHINE = RGB(255, 175, 165);
static const Color COLOR_LEAF = RGB(110, 200, 90);
static const Color COLOR_PANEL = RGB(16, 24, 36);
static const Color COLOR_ACCENT = RGB(100, 210, 110);
static const Color COLOR_ALERT = RGB(235, 90, 80);

enum {
    HUD_TEXT_Y = 18,
    BODY_INSET = 2,
    BODY_SIZE = GAME_CELL_PX - 2 * BODY_INSET,
    BLINK_TICKS = 15,
};

static int cell_px(uint8_t column)
{
    return GAME_BOARD_X + column * GAME_CELL_PX;
}

static int cell_py(uint8_t row)
{
    return GAME_BOARD_Y + row * GAME_CELL_PX;
}

static Color lerp_color(Color a, Color b, int step, int steps)
{
    if (steps <= 0) return a;
    return (Color)RGB((unsigned char)(a.r + (b.r - a.r) * step / steps),
               (unsigned char)(a.g + (b.g - a.g) * step / steps),
               (unsigned char)(a.b + (b.b - a.b) * step / steps));
}

static void draw_centered(const char *text, int y, int size, Color color)
{
    DrawText(text, (GAME_WIDTH - MeasureText(text, size)) / 2, y, size, color);
}

static void draw_panel(int x, int y, int width, int height, Color accent)
{
    DrawRectangle(x - 2, y - 2, width + 4, height + 4, accent);
    DrawRectangle(x, y, width, height, COLOR_PANEL);
}

static void draw_hud(const struct game_t *game)
{
    DrawText("SCORE", GAME_BOARD_X, HUD_TEXT_Y + 7, 16, COLOR_LABEL);
    DrawText(TextFormat("%u", (unsigned)game->score), GAME_BOARD_X + 66, HUD_TEXT_Y, 24,
             COLOR_VALUE);
    unsigned best = game->score > game->best ? game->score : game->best;
    const char *best_text = TextFormat("%u", best);
    int best_x = GAME_BOARD_X + GAME_BOARD_W - MeasureText(best_text, 24);
    DrawText(best_text, best_x, HUD_TEXT_Y, 24, COLOR_VALUE);
    DrawText("BEST", best_x - 8 - MeasureText("BEST", 16), HUD_TEXT_Y + 7, 16, COLOR_LABEL);
    if (game->phase == GAME_PHASE_PLAYING && !game->paused) {
        /* Pause hint: tapping the score bar pauses. */
        DrawRectangle(GAME_WIDTH / 2 - 9, HUD_TEXT_Y + 2, 6, 18, COLOR_LABEL);
        DrawRectangle(GAME_WIDTH / 2 + 3, HUD_TEXT_Y + 2, 6, 18, COLOR_LABEL);
    }
}

static void draw_board(void)
{
    DrawRectangle(GAME_BOARD_X - 3, GAME_BOARD_Y - 3, GAME_BOARD_W + 6, GAME_BOARD_H + 6,
                  COLOR_BORDER);
    DrawRectangle(GAME_BOARD_X, GAME_BOARD_Y, GAME_BOARD_W, GAME_BOARD_H, COLOR_TILE_DARK);
    for (uint8_t row = 0; row < GAME_ROWS; ++row) {
        for (uint8_t column = (uint8_t)(row & 1); column < GAME_COLS; column += 2) {
            DrawRectangle(cell_px(column), cell_py(row), GAME_CELL_PX, GAME_CELL_PX,
                          COLOR_TILE_LIGHT);
        }
    }
}

static void draw_food(const struct game_t *game)
{
    int phase = (int)(game->tick % 30);
    float pulse = (phase < 15 ? phase : 30 - phase) / 15.0f;
    float radius = 7.0f + 1.5f * pulse;
    Vector2 center = {(float)(cell_px(game->food.x) + GAME_CELL_PX / 2),
                      (float)(cell_py(game->food.y) + GAME_CELL_PX / 2)};
    DrawCircleV(center, radius, COLOR_FOOD);
    DrawCircleV((Vector2){center.x - 3, center.y - 3}, 2.2f, COLOR_FOOD_SHINE);
    DrawCircleV((Vector2){center.x + 3, center.y - radius - 1}, 2.5f, COLOR_LEAF);
}

/* Fills the gap between a segment and its tail-side neighbour. */
static void draw_joint(game_cell_t cell, game_cell_t next, Color color)
{
    int x = cell_px(cell.x) + BODY_INSET, y = cell_py(cell.y) + BODY_INSET;
    if (next.x > cell.x) DrawRectangle(x + BODY_SIZE, y, 2 * BODY_INSET, BODY_SIZE, color);
    else if (next.x < cell.x) DrawRectangle(x - 2 * BODY_INSET, y, 2 * BODY_INSET, BODY_SIZE, color);
    else if (next.y > cell.y) DrawRectangle(x, y + BODY_SIZE, BODY_SIZE, 2 * BODY_INSET, color);
    else DrawRectangle(x, y - 2 * BODY_INSET, BODY_SIZE, 2 * BODY_INSET, color);
}

static void draw_head(const struct game_t *game, game_cell_t cell)
{
    static const float FORWARD_X[4] = {0, 1, 0, -1};
    static const float FORWARD_Y[4] = {-1, 0, 1, 0};
    bool dead = game->phase == GAME_PHASE_OVER;
    Rectangle shape = {(float)cell_px(cell.x) + 1, (float)cell_py(cell.y) + 1,
                       GAME_CELL_PX - 2, GAME_CELL_PX - 2};
    DrawRectangleRounded(shape, 0.5f, 6, dead ? COLOR_HEAD_DEAD : COLOR_HEAD);
    float fx = FORWARD_X[game->dir], fy = FORWARD_Y[game->dir];
    float cx = shape.x + shape.width / 2, cy = shape.y + shape.height / 2;
    for (int side = -1; side <= 1; side += 2) {
        Vector2 eye = {cx + fx * 3 - fy * 5 * side, cy + fy * 3 + fx * 5 * side};
        DrawCircleV(eye, 3.5f, COLOR_EYE);
        DrawCircleV((Vector2){eye.x + fx * 1.2f, eye.y + fy * 1.2f}, 1.8f, COLOR_PUPIL);
    }
}

static void draw_snake(const struct game_t *game)
{
    /* Tail first, so segments nearer the head draw on top. */
    for (int i = game->length - 1; i > 0; --i) {
        game_cell_t cell = game_segment(game, (uint16_t)i);
        Color color = lerp_color(COLOR_BODY_NEAR, COLOR_BODY_FAR, i - 1, game->length - 2);
        DrawRectangle(cell_px(cell.x) + BODY_INSET, cell_py(cell.y) + BODY_INSET, BODY_SIZE,
                      BODY_SIZE, color);
        if (i + 1 < game->length) draw_joint(cell, game_segment(game, (uint16_t)(i + 1)), color);
    }
    game_cell_t head = game_segment(game, 0);
    if (game->length > 1) draw_joint(head, game_segment(game, 1), COLOR_BODY_NEAR);
    draw_head(game, head);
}

static bool blink_on(const struct game_t *game)
{
    return (game->tick / BLINK_TICKS) % 2 == 0;
}

static void draw_overlay(const struct game_t *game)
{
    if (game->paused) {
        draw_panel(120, 206, 240, 88, COLOR_LABEL);
        draw_centered("PAUSED", 220, 32, COLOR_VALUE);
        draw_centered("TAP TO RESUME", 264, 16, COLOR_LABEL);
        return;
    }
    switch (game->phase) {
    case GAME_PHASE_READY:
        draw_panel(70, 96, 340, 132, COLOR_ACCENT);
        draw_centered("SNAKE", 112, 48, COLOR_ACCENT);
        draw_centered("SWIPE TO STEER", 172, 16, COLOR_LABEL);
        if (blink_on(game)) draw_centered("TAP TO START", 198, 16, COLOR_VALUE);
        break;
    case GAME_PHASE_OVER:
    case GAME_PHASE_WON: {
        bool won = game->phase == GAME_PHASE_WON;
        /* Keep the crash site visible: use the half of the board away from the head. */
        int top = game_segment(game, 0).y < GAME_ROWS / 2 ? 272 : 90;
        draw_panel(70, top, 340, 172, won ? COLOR_ACCENT : COLOR_ALERT);
        draw_centered(won ? "YOU WIN" : "GAME OVER", top + 18, 32, won ? COLOR_ACCENT : COLOR_ALERT);
        draw_centered(TextFormat("SCORE %u", (unsigned)game->score), top + 64, 24, COLOR_VALUE);
        if (game->new_best) draw_centered("NEW BEST", top + 98, 16, COLOR_ACCENT);
        if (game_restart_ready(game) && blink_on(game))
            draw_centered("TAP TO PLAY AGAIN", top + 140, 16, COLOR_LABEL);
        break;
    }
    case GAME_PHASE_PLAYING:
        break;
    }
}

bool game_render(game_handle_t game)
{
    if (!game) return false;
    BeginDrawing();
    if (!MosaicoFastFrameAvailable()) {
        EndDrawing();
        return false;
    }
    ClearBackground(COLOR_BACKGROUND);
    draw_hud(game);
    draw_board();
    if (game->phase != GAME_PHASE_WON) draw_food(game);
    draw_snake(game);
    draw_overlay(game);
    EndDrawing();
    return true;
}
