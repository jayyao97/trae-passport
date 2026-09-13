#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLOUB_PROFILE_SAMPLES 64
#define BLOUB_MAX_DOTS 8
#define BLOUB_MAX_ARCS 6
#define BLOUB_STAGE_SIZE 224

typedef enum {
    BLOUB_STATE_IDLE = 0,
    BLOUB_STATE_THINKING,
    BLOUB_STATE_WINK,
    BLOUB_STATE_WIDE,
    BLOUB_STATE_ALERT,
    BLOUB_STATE_NOTIFY,
    BLOUB_STATE_EXCLAIM,
    BLOUB_STATE_SLEEP,
    BLOUB_STATE_EGG,
    BLOUB_STATE_HEXAGON,
    BLOUB_STATE_PLAY,
    BLOUB_STATE_ORBIT,
    BLOUB_STATE_BURST,
    BLOUB_STATE_COMET,
    BLOUB_STATE_COUNT,
} bloub_state_t;

typedef enum {
    BLOUB_COLOR_INK = 0,
    BLOUB_COLOR_PAPER,
    BLOUB_COLOR_NOTIFY,
    BLOUB_COLOR_RED,
    BLOUB_COLOR_ORANGE,
    BLOUB_COLOR_YELLOW,
    BLOUB_COLOR_GREEN,
    BLOUB_COLOR_CYAN,
    BLOUB_COLOR_BLUE,
    BLOUB_COLOR_VIOLET,
    BLOUB_COLOR_PINK,
} bloub_color_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    int16_t rotation;
    uint8_t opacity;
} bloub_eye_frame_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t diameter;
    uint8_t opacity;
    uint8_t color;
    bool behind;
} bloub_circle_frame_t;

/* Angles and sweep use 0..63 phases; phase_q8 retains sub-phase motion. */
typedef struct {
    int32_t center_x_q8;
    int32_t center_y_q8;
    int16_t radius_x;
    int16_t radius_y;
    uint8_t tilt;
    uint16_t phase_q8;
    uint8_t sweep;
    uint8_t width;
    uint8_t opacity;
    uint16_t hue;
    uint16_t hue_span;
    bool depth_sorted;
} bloub_arc_frame_t;

typedef struct {
    int16_t x;
    int16_t y;
} bloub_point_t;

typedef struct {
    bloub_point_t body_points[BLOUB_PROFILE_SAMPLES];
    int16_t body_center_x;
    int16_t body_center_y;
    uint8_t body_opacity;
    bloub_eye_frame_t eyes[2];
    bloub_circle_frame_t dots[BLOUB_MAX_DOTS];
    bloub_arc_frame_t arcs[BLOUB_MAX_ARCS];
    uint8_t dot_count;
    uint8_t arc_count;
} bloub_frame_t;

const char *bloub_state_name(bloub_state_t state);
uint32_t bloub_state_duration_ms(bloub_state_t state);
uint32_t bloub_state_morph_ms(bloub_state_t state);
void bloub_sample(bloub_state_t state, uint32_t elapsed_ms, bloub_frame_t *frame);
void bloub_blend(const bloub_frame_t *from, const bloub_frame_t *to,
                 uint16_t mix, bloub_frame_t *frame);

/* Pure geometry helpers shared by the renderer and host regression tests. */
bool bloub_scanline_span(const bloub_point_t *points, size_t count, int16_t y,
                         int16_t *left, int16_t *right);
void bloub_trig_q14(uint8_t phase, int16_t *cosine, int16_t *sine);
uint16_t bloub_zero_crossing_q8(int16_t from, int16_t to);
