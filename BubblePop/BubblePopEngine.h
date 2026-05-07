#ifndef BUBBLE_POP_ENGINE_H
#define BUBBLE_POP_ENGINE_H

#include <stdint.h>
#include "Joystick.h"

#define BP_ROWS 8
#define BP_COLS 10
#define BP_EMPTY 255
#define BP_COLOUR_COUNT 7

typedef enum {
    BP_STATE_START = 0,
    BP_STATE_COUNTDOWN,
    BP_STATE_PLAYING,
    BP_STATE_WIN,
    BP_STATE_GAMEOVER
} BubblePopState_t;

typedef struct {
    uint8_t colour;
    uint8_t active;
    float x;
    float y;
    float vx;
    float vy;
} BPBubble_t;

typedef struct {
    uint8_t grid[BP_ROWS][BP_COLS];

    BPBubble_t current;
    BPBubble_t next;

    int score;
    int counts_left;
    int timer_seconds;

    float aim_angle_deg;
    BubblePopState_t state;

    uint8_t finished;
    uint8_t warning_active;
    uint8_t shoot_latch;
    uint8_t button_latch;
    uint8_t swap_latch;
    
    uint8_t pop_anim_active;
    uint32_t pop_anim_start;
    int pop_anim_x;
    int pop_anim_y;
    uint8_t pop_anim_colour;
    uint8_t miss_count;
    uint16_t star_offset;

    uint32_t last_second_tick;
    uint32_t last_countdown_tick;
} BubblePopEngine_t;

void BubblePopEngine_Init(BubblePopEngine_t *game);
void BubblePopEngine_Update(BubblePopEngine_t *game, UserInput input);
void BubblePopEngine_Draw(BubblePopEngine_t *game);
uint8_t BubblePopEngine_IsFinished(BubblePopEngine_t *game);

#endif