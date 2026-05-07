#include "BubblePopEngine.h"
#include "main.h"
#include "LCD.h"
#include "Buzzer.h"
#include "PWM.h"
#include "rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;
extern PWM_cfg_t pwm_cfg;

/* =========================================================
   GAME CONFIGURATION
   ========================================================= */

#define BP_SCREEN_W       240
#define BP_SCREEN_H       280

#define BP_PLAY_TOP       24
#define BP_WARNING_Y      178

#define BP_RADIUS         10
#define BP_DIAMETER       (BP_RADIUS * 2)

#define BP_GRID_X0        18
#define BP_GRID_Y0        38

#define BP_SHOOTER_X      120
#define BP_SHOOTER_Y      205
#define BP_AIM_LEN        75

#define BP_SHOT_SPEED     12.0f
#define BP_COUNTS_TOTAL   30
#define BP_START_TIME     120

#define BP_POP_ANIM_MS      180
#define BP_MISSES_PER_DROP  3

#define BP_POP_FLASH_COUNT      3
#define BP_POP_FLASH_DELAY_MS   40
#define BP_GAMEOVER_FLASH_MS    400

#define BP_BTN_PORT GPIOC
#define BP_BTN_PIN  GPIO_PIN_13  // shoot button

#define BP_SWAP_PORT      GPIOC
#define BP_SWAP_PIN       GPIO_PIN_8

#define BP_LED_GREEN_PORT GPIOA
#define BP_LED_GREEN_PIN  GPIO_PIN_11

#define BP_LED_RED_PORT   GPIOA
#define BP_LED_RED_PIN    GPIO_PIN_12

/* =========================================================
   COLOUR INDEXES
   ========================================================= */

enum {
    BP_BLACK   = 0,
    BP_WHITE   = 1,
    BP_RED     = 2,
    BP_GREEN   = 3,
    BP_BLUE    = 4,
    BP_ORANGE  = 5,
    BP_YELLOW  = 6,
    BP_PINK    = 7,
    BP_PURPLE  = 8,
    BP_NAVY    = 9,
    BP_GOLD    = 10,
    BP_VIOLET  = 11,
    BP_BROWN   = 12,
    BP_GREY    = 13,
    BP_CYAN    = 14,
    BP_MAGENTA = 15
};

static const uint8_t bp_palette[BP_COLOUR_COUNT] = {
    BP_RED, BP_BLUE, BP_YELLOW, BP_GREEN, BP_ORANGE, BP_PINK, BP_PURPLE
};

/* =========================================================
   INPUT AND RANDOM HELPERS
   ========================================================= */

static void bp_configure_extra_inputs(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = BP_SWAP_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BP_SWAP_PORT, &GPIO_InitStruct);
}

static uint8_t bp_button_pressed(void) {
    return (HAL_GPIO_ReadPin(BP_BTN_PORT, BP_BTN_PIN) == GPIO_PIN_RESET) ? 1u : 0u;
}

static uint8_t bp_swap_button_pressed(void) {
    return (HAL_GPIO_ReadPin(BP_SWAP_PORT, BP_SWAP_PIN) == GPIO_PIN_RESET) ? 1u : 0u;
}

static void bp_swap_current_and_next(BubblePopEngine_t *game) {
    uint8_t temp = game->current.colour;
    game->current.colour = game->next.colour;
    game->next.colour = temp;
}

static uint32_t bp_random_u32(void) {
    uint32_t value = 0;

    if (HAL_RNG_GenerateRandomNumber(&hrng, &value) == HAL_OK) {
        return value;
    }

    return (uint32_t)HAL_GetTick();
}

static uint8_t bp_random_colour(void) {
    return bp_palette[bp_random_u32() % BP_COLOUR_COUNT];
}

/* Selects a random colour that still exists on the board */
static uint8_t bp_random_existing_colour(BubblePopEngine_t *game) {
    uint8_t colours[BP_ROWS * BP_COLS];
    int count = 0;

    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            uint8_t colour = game->grid[r][c];

            if (colour != BP_EMPTY) {
                colours[count++] = colour;
            }
        }
    }

    if (count == 0) {
        return bp_random_colour();
    }

    return colours[bp_random_u32() % count];
}

static uint8_t bp_colour_exists_on_board(BubblePopEngine_t *game, uint8_t colour) {
    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            if (game->grid[r][c] == colour) {
                return 1;
            }
        }
    }
    return 0;
}

/* Removes invalid current/next colours after a colour disappears */
static void bp_refresh_shooter_colours(BubblePopEngine_t *game) {
    if (!bp_colour_exists_on_board(game, game->current.colour)) {
        game->current.colour = bp_random_existing_colour(game);
    }

    if (!bp_colour_exists_on_board(game, game->next.colour)) {
        game->next.colour = bp_random_existing_colour(game);
    }
}

/* =========================================================
   BASIC OUTPUT HELPERS
   ========================================================= */

static void bp_present(void) {
    LCD_Refresh(&cfg0);
}

static void bp_text(const char *s, int x, int y, int colour, int scale) {
    LCD_printString((char*)s, (uint16_t)x, (uint16_t)y, (uint8_t)colour, (uint8_t)scale);
}

static void bp_green_led_on(void) {
    HAL_GPIO_WritePin(BP_LED_GREEN_PORT, BP_LED_GREEN_PIN, GPIO_PIN_SET);
}

static void bp_green_led_off(void) {
    HAL_GPIO_WritePin(BP_LED_GREEN_PORT, BP_LED_GREEN_PIN, GPIO_PIN_RESET);
}

static void bp_red_led_on(void) {
    HAL_GPIO_WritePin(BP_LED_RED_PORT, BP_LED_RED_PIN, GPIO_PIN_SET);
}

static void bp_red_led_off(void) {
    HAL_GPIO_WritePin(BP_LED_RED_PORT, BP_LED_RED_PIN, GPIO_PIN_RESET);
}

static void bp_beep_short(uint32_t note, uint8_t volume, uint32_t ms) {
    buzzer_note(&buzzer_cfg, (Buzzer_Note_t)note, volume);
    HAL_Delay(ms);
    buzzer_off(&buzzer_cfg);
}

static void bp_beep_start(void) {
    bp_beep_short(NOTE_C5, 35, 70);
    bp_beep_short(NOTE_E5, 35, 70);
    bp_beep_short(NOTE_G5, 35, 90);
}

static void bp_beep_pop(void) {
    bp_beep_short(NOTE_C6, 30, 25);
}

static void bp_beep_warning(void) {
    bp_beep_short(NOTE_A4, 20, 35);
}

static void bp_beep_win(void) {
    bp_beep_short(NOTE_C5, 35, 70);
    bp_beep_short(NOTE_E5, 35, 70);
    bp_beep_short(NOTE_G5, 35, 70);
    bp_beep_short(NOTE_C6, 40, 100);
}

static void bp_beep_gameover(void) {
    bp_beep_short(NOTE_G4, 35, 90);
    bp_beep_short(NOTE_E4, 35, 90);
    bp_beep_short(NOTE_C4, 35, 130);
}

/* =========================================================
   GRID AND GAME HELPERS
   ========================================================= */

static void bp_reset_current_bubble(BubblePopEngine_t *game, uint8_t colour) {
    game->current.colour = colour;
    game->current.active = 0;
    game->current.x = BP_SHOOTER_X;
    game->current.y = BP_SHOOTER_Y;
    game->current.vx = 0.0f;
    game->current.vy = 0.0f;
}

static void bp_grid_to_xy(int row, int col, int *x, int *y) {
    int row_offset = (row % 2) ? BP_RADIUS : 0;

    *x = BP_GRID_X0 + row_offset + (col * BP_DIAMETER);
    *y = BP_GRID_Y0 + (row * BP_DIAMETER);
}

static float bp_distance_sq(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return dx * dx + dy * dy;
}

static int bp_in_bounds(int r, int c) {
    return (r >= 0 && r < BP_ROWS && c >= 0 && c < BP_COLS);
}

/* Initial board: 4 rows, grouped in pairs */
static void bp_seed_grid(BubblePopEngine_t *game) {
    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            game->grid[r][c] = BP_EMPTY;
        }
    }

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < BP_COLS; c += 2) {
            uint8_t colour = bp_random_colour();
            game->grid[r][c] = colour;

            if (c + 1 < BP_COLS) {
                game->grid[r][c + 1] = colour;
            }
        }
    }
}

static int bp_any_bubbles_left(BubblePopEngine_t *game) {
    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            if (game->grid[r][c] != BP_EMPTY) {
                return 1;
            }
        }
    }
    return 0;
}

static void bp_update_warning(BubblePopEngine_t *game) {
    game->warning_active = 0;

    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            if (game->grid[r][c] == BP_EMPTY) continue;

            int x, y;
            bp_grid_to_xy(r, c, &x, &y);

            if (y + BP_RADIUS >= BP_WARNING_Y) {
                game->warning_active = 1;
                return;
            }
        }
    }
}

/* Converts joystick angle into aiming angle */
static void bp_update_aim_from_input(BubblePopEngine_t *game, UserInput input) {
    if (input.magnitude < 0.10f) return;

    float a = input.angle;

    if (a > 180.0f) {
        a -= 360.0f;
    }

    if (a < -75.0f) a = -75.0f;
    if (a > 75.0f) a = 75.0f;

    game->aim_angle_deg = a;
}

/* Fires the current bubble */
static void bp_fire_current(BubblePopEngine_t *game) {
    if (game->current.active) return;
    if (game->counts_left <= 0) return;
    if (game->state != BP_STATE_PLAYING) return;

    float rad = game->aim_angle_deg * (float)M_PI / 180.0f;

    game->current.active = 1;
    game->current.x = BP_SHOOTER_X;
    game->current.y = BP_SHOOTER_Y;
    game->current.vx = BP_SHOT_SPEED * sinf(rad);
    game->current.vy = -BP_SHOT_SPEED * cosf(rad);

    game->counts_left--;
}

/* Drops all rows after repeated misses */
static void bp_drop_rows_down(BubblePopEngine_t *game) {
    for (int r = BP_ROWS - 1; r > 0; r--) {
        for (int c = 0; c < BP_COLS; c++) {
            game->grid[r][c] = game->grid[r - 1][c];
        }
    }

    for (int c = 0; c < BP_COLS; c += 2) {
        uint8_t colour = bp_random_existing_colour(game);

        game->grid[0][c] = colour;

        if (c + 1 < BP_COLS) {
            game->grid[0][c + 1] = colour;
        }
    }
}

/* Attach to closest grid cell, mainly used for ceiling hit */
static void bp_attach_to_nearest_cell(BubblePopEngine_t *game, int *out_r, int *out_c) {
    float best = 1e9f;
    int best_r = 0;
    int best_c = 0;

    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            if (game->grid[r][c] != BP_EMPTY) continue;

            int gx, gy;
            bp_grid_to_xy(r, c, &gx, &gy);

            float d = bp_distance_sq(game->current.x, game->current.y, (float)gx, (float)gy);

            if (d < best) {
                best = d;
                best_r = r;
                best_c = c;
            }
        }
    }

    game->grid[best_r][best_c] = game->current.colour;
    *out_r = best_r;
    *out_c = best_c;

    bp_reset_current_bubble(game, game->next.colour);
    game->next.colour = bp_random_existing_colour(game);
}

/* =========================================================
   MATCHING AND POPPING
   ========================================================= */

static int bp_collect_match(BubblePopEngine_t *game,
                            int sr, int sc,
                            int target,
                            uint8_t visited[BP_ROWS][BP_COLS],
                            int cells[BP_ROWS * BP_COLS][2]) {
    int queue[BP_ROWS * BP_COLS][2];
    int qh = 0;
    int qt = 0;
    int count = 0;

    queue[qt][0] = sr;
    queue[qt][1] = sc;
    qt++;
    visited[sr][sc] = 1;

    while (qh < qt) {
        int r = queue[qh][0];
        int c = queue[qh][1];
        qh++;

        cells[count][0] = r;
        cells[count][1] = c;
        count++;

        int neighbours[6][2];

        if (r % 2 == 0) {
            int temp[6][2] = {
                {r, c - 1}, {r, c + 1},
                {r - 1, c - 1}, {r - 1, c},
                {r + 1, c - 1}, {r + 1, c}
            };

            for (int i = 0; i < 6; i++) {
                neighbours[i][0] = temp[i][0];
                neighbours[i][1] = temp[i][1];
            }
        } else {
            int temp[6][2] = {
                {r, c - 1}, {r, c + 1},
                {r - 1, c}, {r - 1, c + 1},
                {r + 1, c}, {r + 1, c + 1}
            };

            for (int i = 0; i < 6; i++) {
                neighbours[i][0] = temp[i][0];
                neighbours[i][1] = temp[i][1];
            }
        }

        for (int i = 0; i < 6; i++) {
            int nr = neighbours[i][0];
            int nc = neighbours[i][1];

            if (!bp_in_bounds(nr, nc)) continue;
            if (visited[nr][nc]) continue;
            if (game->grid[nr][nc] != target) continue;

            visited[nr][nc] = 1;
            queue[qt][0] = nr;
            queue[qt][1] = nc;
            qt++;
        }
    }

    return count;
}

/* Starts a short explosion effect */
static void bp_start_pop_animation(BubblePopEngine_t *game, int x, int y, uint8_t colour) {
    game->pop_anim_active = 1;
    game->pop_anim_start = HAL_GetTick();
    game->pop_anim_x = x;
    game->pop_anim_y = y;
    game->pop_anim_colour = colour;
}

/* Returns 1 if bubbles popped, 0 if it was a miss */
static uint8_t bp_check_and_pop(BubblePopEngine_t *game, int row, int col) {
    if (game->grid[row][col] == BP_EMPTY) {
        return 0;
    }

    uint8_t visited[BP_ROWS][BP_COLS] = {0};
    int cells[BP_ROWS * BP_COLS][2];
    int colour = game->grid[row][col];

    int count = bp_collect_match(game, row, col, colour, visited, cells);

    if (count >= 3) {
        int anim_x, anim_y;
        bp_grid_to_xy(row, col, &anim_x, &anim_y);
        bp_start_pop_animation(game, anim_x, anim_y, colour);

        for (int i = 0; i < count; i++) {
            int r = cells[i][0];
            int c = cells[i][1];
            game->grid[r][c] = BP_EMPTY;
        }

        game->score += (count * 10);

        bp_beep_pop();
        
        for (int i = 0; i < BP_POP_FLASH_COUNT; i++) {
             bp_green_led_on();
             HAL_Delay(BP_POP_FLASH_DELAY_MS);
             bp_green_led_off();
             HAL_Delay(BP_POP_FLASH_DELAY_MS);
        }

        bp_refresh_shooter_colours(game);
        return 1;
    }

    return 0;
}

/* Handles miss counter and row dropping */
static void bp_process_pop_or_miss(BubblePopEngine_t *game, int row, int col) {
    if (!bp_check_and_pop(game, row, col)) {
        game->miss_count++;

        if (game->miss_count >= BP_MISSES_PER_DROP) {
            game->miss_count = 0;
            bp_drop_rows_down(game);
        }
    } else {
        game->miss_count = 0;
    }
}

/* =========================================================
   ACTIVE BUBBLE UPDATE
   ========================================================= */

static void bp_update_current_bubble(BubblePopEngine_t *game) {
    if (!game->current.active) return;

    game->current.x += game->current.vx;
    game->current.y += game->current.vy;

    if (game->current.x <= (BP_RADIUS + 2)) {
        game->current.x = BP_RADIUS + 2;
        game->current.vx = -game->current.vx;
    }

    if (game->current.x >= (BP_SCREEN_W - BP_RADIUS - 2)) {
        game->current.x = BP_SCREEN_W - BP_RADIUS - 2;
        game->current.vx = -game->current.vx;
    }

    if (game->current.y <= (BP_PLAY_TOP + BP_RADIUS)) {
        int rr, cc;
        bp_attach_to_nearest_cell(game, &rr, &cc);
        bp_process_pop_or_miss(game, rr, cc);
        return;
    }

    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            if (game->grid[r][c] == BP_EMPTY) continue;

            int gx, gy;
            bp_grid_to_xy(r, c, &gx, &gy);

            if (bp_distance_sq(game->current.x, game->current.y, (float)gx, (float)gy)
                <= (float)(BP_DIAMETER * BP_DIAMETER)) {

                int target_r = -1;
                int target_c = -1;
                int neighbours[6][2];

                if (r % 2 == 0) {
                    int temp[6][2] = {
                        {r, c - 1}, {r, c + 1},
                        {r - 1, c - 1}, {r - 1, c},
                        {r + 1, c - 1}, {r + 1, c}
                    };

                    for (int i = 0; i < 6; i++) {
                        neighbours[i][0] = temp[i][0];
                        neighbours[i][1] = temp[i][1];
                    }
                } else {
                    int temp[6][2] = {
                        {r, c - 1}, {r, c + 1},
                        {r - 1, c}, {r - 1, c + 1},
                        {r + 1, c}, {r + 1, c + 1}
                    };

                    for (int i = 0; i < 6; i++) {
                        neighbours[i][0] = temp[i][0];
                        neighbours[i][1] = temp[i][1];
                    }
                }

                float best = 1e9f;

                for (int i = 0; i < 6; i++) {
                    int nr = neighbours[i][0];
                    int nc = neighbours[i][1];

                    if (!bp_in_bounds(nr, nc)) continue;
                    if (game->grid[nr][nc] != BP_EMPTY) continue;

                    int gx2, gy2;
                    bp_grid_to_xy(nr, nc, &gx2, &gy2);

                    float d = bp_distance_sq(game->current.x, game->current.y, (float)gx2, (float)gy2);

                    if (d < best) {
                        best = d;
                        target_r = nr;
                        target_c = nc;
                    }
                }

                if (target_r != -1) {
                    game->grid[target_r][target_c] = game->current.colour;

                    bp_reset_current_bubble(game, game->next.colour);
                    game->next.colour = bp_random_existing_colour(game);

                    bp_process_pop_or_miss(game, target_r, target_c);
                }

                return;
            }
        }
    }
}

/* =========================================================
   WIN / LOSE CONDITIONS
   ========================================================= */

static uint8_t bp_reached_warning_line(BubblePopEngine_t *game) {
    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            if (game->grid[r][c] == BP_EMPTY) continue;

            int x, y;
            bp_grid_to_xy(r, c, &x, &y);

            if (y + BP_RADIUS >= BP_WARNING_Y) {
                return 1;
            }
        }
    }

    return 0;
}

static void bp_handle_end_conditions(BubblePopEngine_t *game) {
    if (!bp_any_bubbles_left(game)) {
        game->state = BP_STATE_WIN;
        game->finished = 1;
        bp_green_led_on();
        bp_beep_win();
        return;
    }

    if (bp_reached_warning_line(game)) {
        game->state = BP_STATE_GAMEOVER;
        game->finished = 1;
        bp_red_led_on();
        bp_beep_gameover();
        return;
    }

    if (game->timer_seconds <= 0 || game->counts_left <= 0) {
        game->state = BP_STATE_GAMEOVER;
        game->finished = 1;
        bp_red_led_on();
        bp_beep_gameover();
    }
}

/* =========================================================
   DRAWING HELPERS
   ========================================================= */

static void bp_draw_bubble_at(int x, int y, uint8_t colour) {
    LCD_Draw_Circle((uint16_t)x, (uint16_t)y, BP_RADIUS, colour, 1);
    LCD_Draw_Circle((uint16_t)x, (uint16_t)y, BP_RADIUS, BP_WHITE, 0);
}

static void bp_draw_grid(BubblePopEngine_t *game) {
    for (int r = 0; r < BP_ROWS; r++) {
        for (int c = 0; c < BP_COLS; c++) {
            if (game->grid[r][c] == BP_EMPTY) continue;

            int x, y;
            bp_grid_to_xy(r, c, &x, &y);
            bp_draw_bubble_at(x, y, game->grid[r][c]);
        }
    }
}

static void bp_draw_hud(BubblePopEngine_t *game) {
    char text[32];

    LCD_Draw_Rect(0, 0, 240, 20, BP_BLACK, 1);
    LCD_Draw_Line(0, 20, 239, 20, BP_WHITE);

    sprintf(text, "Time:%02d", game->timer_seconds);
    bp_text(text, 5, 5, BP_WHITE, 1);

    sprintf(text, "Shots:%d", game->counts_left);
    bp_text(text, 88, 5, BP_WHITE, 1);

    sprintf(text, "Score:%d", game->score);
    bp_text(text, 170, 5, BP_WHITE, 1);

    LCD_Draw_Rect(182, 208, 50, 28, BP_BLACK, 1);
    LCD_Draw_Rect(182, 208, 50, 28, BP_WHITE, 0);
    bp_text("Next", 186, 212, BP_WHITE, 1);

    LCD_Draw_Circle(220, 224, 8, game->next.colour, 1);
    LCD_Draw_Circle(220, 224, 8, BP_WHITE, 0);
}

static void bp_draw_warning_line(BubblePopEngine_t *game) {
    uint8_t colour = game->warning_active ? BP_RED : BP_WHITE;

    for (int x = 5; x < 235; x += 10) {
        LCD_Draw_Line(x, BP_WARNING_Y, x + 5, BP_WARNING_Y, colour);
    }
}

static void bp_draw_current_and_next(BubblePopEngine_t *game) {
    bp_draw_bubble_at((int)game->current.x, (int)game->current.y, game->current.colour);
}

static void bp_draw_aim(BubblePopEngine_t *game) {
    float rad = game->aim_angle_deg * (float)M_PI / 180.0f;

    int x2 = BP_SHOOTER_X + (int)(BP_AIM_LEN * sinf(rad));
    int y2 = BP_SHOOTER_Y - (int)(BP_AIM_LEN * cosf(rad));

    int segments = 6;

    for (int i = 0; i < segments; i++) {
        if (i % 2 == 0) {
            int sx = BP_SHOOTER_X + ((x2 - BP_SHOOTER_X) * i) / segments;
            int sy = BP_SHOOTER_Y + ((y2 - BP_SHOOTER_Y) * i) / segments;
            int ex = BP_SHOOTER_X + ((x2 - BP_SHOOTER_X) * (i + 1)) / segments;
            int ey = BP_SHOOTER_Y + ((y2 - BP_SHOOTER_Y) * (i + 1)) / segments;

            LCD_Draw_Line(sx, sy, ex, ey, BP_WHITE);
        }
    }
}

/* Moving parallax stars */
static void bp_draw_space_stars(BubblePopEngine_t *game) {
    int offset = game->star_offset % 240;

    int stars[][2] = {
    {12, 14}, {30, 28}, {48, 10}, {62, 40},
    {84, 18}, {102, 32}, {120, 12}, {145, 24},
    {166, 14}, {188, 34}, {210, 16}, {226, 30},

    {20, 70}, {52, 92}, {80, 76}, {118, 88},
    {154, 70}, {196, 82}, {224, 96},

    {10, 115}, {35, 135}, {66, 118}, {95, 145},
    {130, 122}, {160, 140}, {205, 125}, {232, 150},

    {15, 175}, {45, 195}, {75, 165}, {108, 205},
    {138, 178}, {168, 198}, {202, 170}, {230, 210},

    {25, 235}, {58, 250}, {92, 228}, {125, 260},
    {155, 235}, {190, 255}, {220, 230}
    };

    for (int i = 0; i < 44; i++) {
        int x = (stars[i][0] + offset) % 240;
        int y = stars[i][1];

        LCD_Set_Pixel(x, y, BP_WHITE);
    }
}

/* Space background with planets and platform */
static void bp_draw_space_background(BubblePopEngine_t *game) {
    LCD_Fill_Buffer(BP_NAVY);

    bp_draw_space_stars(game);

    // Big ring planet
    LCD_Draw_Circle(200, 45, 22, BP_PURPLE, 0);
    LCD_Draw_Circle(200, 45, 18, BP_PURPLE, 0);
    LCD_Draw_Circle(200, 45, 14, BP_WHITE, 0);
    LCD_Draw_Circle(200, 45, 24, BP_BLUE, 0);
    LCD_Draw_Line(170, 30, 230, 60, BP_WHITE);
    LCD_Draw_Line(170, 32, 230, 62, BP_CYAN);
    LCD_Draw_Line(170, 34, 230, 64, BP_WHITE);
    // Small ring planet
    LCD_Draw_Circle(45, 110, 16, BP_CYAN, 0);
    LCD_Draw_Circle(45, 110, 12, BP_CYAN, 0);
    LCD_Draw_Line(15, 94, 75, 124, BP_WHITE);
    LCD_Draw_Line(15, 96, 75, 126, BP_CYAN);

    // Distant planet
    LCD_Draw_Circle(170, 80, 10, BP_ORANGE, 0);
    LCD_Draw_Circle(170, 80, 7, BP_WHITE, 0);

    // Space platform
    LCD_Draw_Line(0, 165, 239, 165, BP_BLUE);
    LCD_Draw_Line(0, 166, 239, 166, BP_BLUE);

    for (int y = 185; y <= 239; y++) {
        LCD_Draw_Line(0, y, 239, y, BP_GREY);
    }

    LCD_Draw_Circle(35, 210, 10, BP_BLACK, 0);
    LCD_Draw_Circle(80, 225, 7, BP_BLACK, 0);
    LCD_Draw_Circle(175, 214, 9, BP_BLACK, 0);
}

/* Spaceship launcher */
static void bp_draw_ship_launcher(void) {
    int cx = BP_SHOOTER_X;
    int cy = BP_SHOOTER_Y + 20;

    LCD_Draw_Circle(cx, cy, 16, BP_GREY, 1);
    LCD_Draw_Circle(cx, cy, 16, BP_WHITE, 0);

    LCD_Draw_Circle(cx, cy - 4, 6, BP_CYAN, 1);
    LCD_Draw_Circle(cx, cy - 4, 6, BP_WHITE, 0);

    LCD_Draw_Line(cx - 10, cy + 2, cx - 28, cy + 10, BP_WHITE);
    LCD_Draw_Line(cx - 28, cy + 10, cx - 16, cy + 18, BP_WHITE);
    LCD_Draw_Line(cx - 16, cy + 18, cx - 8, cy + 8, BP_WHITE);

    LCD_Draw_Line(cx + 10, cy + 2, cx + 28, cy + 10, BP_WHITE);
    LCD_Draw_Line(cx + 28, cy + 10, cx + 16, cy + 18, BP_WHITE);
    LCD_Draw_Line(cx + 16, cy + 18, cx + 8, cy + 8, BP_WHITE);

    LCD_Draw_Circle(cx, cy + 12, 4, BP_ORANGE, 1);
    LCD_Draw_Circle(cx, cy + 18, 2, BP_YELLOW, 1);
}

/* Pop / explosion animation */
static void bp_draw_pop_animation(BubblePopEngine_t *game) {
    if (!game->pop_anim_active) return;

    uint32_t elapsed = HAL_GetTick() - game->pop_anim_start;

    if (elapsed > BP_POP_ANIM_MS) {
        game->pop_anim_active = 0;
        return;
    }

    int r = 4 + (elapsed / 30);

    LCD_Draw_Circle(game->pop_anim_x, game->pop_anim_y, r, BP_WHITE, 0);
    LCD_Draw_Circle(game->pop_anim_x, game->pop_anim_y, r + 3, game->pop_anim_colour, 0);

    LCD_Draw_Line(game->pop_anim_x - r, game->pop_anim_y,
                  game->pop_anim_x - r - 5, game->pop_anim_y, BP_WHITE);

    LCD_Draw_Line(game->pop_anim_x + r, game->pop_anim_y,
                  game->pop_anim_x + r + 5, game->pop_anim_y, BP_WHITE);

    LCD_Draw_Line(game->pop_anim_x, game->pop_anim_y - r,
                  game->pop_anim_x, game->pop_anim_y - r - 5, BP_WHITE);

    LCD_Draw_Line(game->pop_anim_x, game->pop_anim_y + r,
                  game->pop_anim_x, game->pop_anim_y + r + 5, BP_WHITE);
}

/* =========================================================
   PUBLIC API
   ========================================================= */

void BubblePopEngine_Init(BubblePopEngine_t *game) {
    bp_configure_extra_inputs();

    bp_seed_grid(game);

    game->score = 0;
    game->counts_left = BP_COUNTS_TOTAL;
    game->timer_seconds = BP_START_TIME;
    game->aim_angle_deg = 0.0f;
    game->state = BP_STATE_START;

    game->finished = 0;
    game->warning_active = 0;
    game->shoot_latch = 0;
    game->button_latch = 0;
    game->swap_latch = 0;

    game->pop_anim_active = 0;
    game->pop_anim_start = 0;
    game->pop_anim_x = 0;
    game->pop_anim_y = 0;
    game->pop_anim_colour = BP_WHITE;

    game->miss_count = 0;
    game->star_offset = 0;

    game->last_second_tick = HAL_GetTick();
    game->last_countdown_tick = HAL_GetTick();

    bp_reset_current_bubble(game, bp_random_existing_colour(game));
    game->next.colour = bp_random_existing_colour(game);

    bp_green_led_off();
    bp_red_led_off();

    PWM_Init(&pwm_cfg);
    PWM_SetFreq(&pwm_cfg, 1000);
    PWM_SetDuty(&pwm_cfg, 0);
}

void BubblePopEngine_Update(BubblePopEngine_t *game, UserInput input) {
    uint32_t now = HAL_GetTick();

    uint8_t button_now = bp_button_pressed();
    uint8_t swap_now = bp_swap_button_pressed();

    // Move stars slowly
    if ((now % 4) == 0) {
        game->star_offset++;
    }

    if (game->state == BP_STATE_START) {
        if (button_now && !game->button_latch) {
            game->state = BP_STATE_COUNTDOWN;
            game->last_countdown_tick = now;
            game->button_latch = 1;
        }

        if (!button_now) {
            game->button_latch = 0;
        }

        return;
    }

    if (game->state == BP_STATE_COUNTDOWN) {
        if (now - game->last_countdown_tick >= 3000) {
            game->state = BP_STATE_PLAYING;
            game->last_second_tick = now;
            bp_beep_start();
        }

        return;
    }

    if (game->state == BP_STATE_WIN || game->state == BP_STATE_GAMEOVER) {

    if (game->state == BP_STATE_GAMEOVER) {
        if ((now / BP_GAMEOVER_FLASH_MS) % 2) {
            bp_red_led_on();
        } else {
            bp_red_led_off();
        }
    }

    if (button_now && !game->button_latch) {
        BubblePopEngine_Init(game);
        game->button_latch = 1;
    }

    if (!button_now) {
        game->button_latch = 0;
    }

    return;
}

    if (game->state != BP_STATE_PLAYING) {
        return;
    }

    // Timer countdown
    if (now - game->last_second_tick >= 1000) {
        game->last_second_tick = now;

        if (game->timer_seconds > 0) {
            game->timer_seconds--;
        }
    }

    bp_update_aim_from_input(game, input);

    // Swap current and next bubble
    if (!game->current.active) {
        if (swap_now && !game->swap_latch) {
            bp_swap_current_and_next(game);
            game->swap_latch = 1;
        }

        if (!swap_now) {
            game->swap_latch = 0;
        }
    }

    // Fire bubble
    if (button_now && !game->shoot_latch) {
        bp_fire_current(game);
        game->shoot_latch = 1;
    }

    if (!button_now) {
        game->shoot_latch = 0;
    }

    bp_update_current_bubble(game);
    bp_update_warning(game);

    if (game->warning_active) {
    bp_beep_warning();
    PWM_SetDuty(&pwm_cfg, 50);
    bp_red_led_on();
    } else {
    PWM_SetDuty(&pwm_cfg, 0);
    bp_red_led_off();
    }

    bp_handle_end_conditions(game);
}

void BubblePopEngine_Draw(BubblePopEngine_t *game) {
    bp_draw_space_background(game);

    if (game->state == BP_STATE_START) {
        bp_text("PLANET POP", 35, 70, BP_CYAN, 2);
        bp_text("Shooting = blue button ", 85, 105, BP_WHITE, 1);
        bp_text("Joystick = aim", 85, 122, BP_WHITE, 1);
        bp_text("Board button = swap balls", 85, 139, BP_WHITE, 1);

        bp_present();
        return;
    }

    if (game->state == BP_STATE_COUNTDOWN) {
        char text[16];

        uint32_t elapsed_ms = HAL_GetTick() - game->last_countdown_tick;
        int seconds_left = 3 - (int)(elapsed_ms / 1000);

        if (seconds_left < 1) {
            seconds_left = 1;
        }

        sprintf(text, "%d", seconds_left);

        bp_text("READY", 92, 95, BP_WHITE, 1);
        bp_text(text, 113, 115, BP_GOLD, 2);

        bp_present();
        return;
    }

    bp_draw_hud(game);
    bp_draw_warning_line(game);
    bp_draw_grid(game);
    bp_draw_pop_animation(game);

    if (!game->current.active) {
        bp_draw_aim(game);
    }

    bp_draw_ship_launcher();
    bp_draw_current_and_next(game);

    if (game->state == BP_STATE_WIN || game->state == BP_STATE_GAMEOVER) {
        LCD_Draw_Rect(35, 92, 170, 46, BP_BLACK, 1);
        LCD_Draw_Rect(35, 92, 170, 46, BP_WHITE, 0);

        if (game->state == BP_STATE_WIN) {
            bp_text("YOU WIN", 72, 102, BP_YELLOW, 2);
        } else {
            bp_text("GAME OVER", 55, 102, BP_RED, 2);
        }

        bp_text("Press button", 72, 124, BP_WHITE, 1);
    }

    bp_present();
}

uint8_t BubblePopEngine_IsFinished(BubblePopEngine_t *game) {
    return game->finished;
}
