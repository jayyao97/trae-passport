/* Fixed-point adaptation of jeremy-prt/bloub's measured state catalogue (MIT). */
#include "bloub_math.h"

#include <limits.h>
#include <string.h>

#define STAGE_CENTER (BLOUB_STAGE_SIZE / 2)
#define BODY_RADIUS 71

static const int16_t COS_Q14[BLOUB_PROFILE_SAMPLES] = {
    16384, 16305, 16069, 15679, 15137, 14449, 13623, 12665,
    11585, 10394, 9102, 7723, 6270, 4756, 3196, 1606,
    0, -1606, -3196, -4756, -6270, -7723, -9102, -10394,
    -11585, -12665, -13623, -14449, -15137, -15679, -16069, -16305,
    -16384, -16305, -16069, -15679, -15137, -14449, -13623, -12665,
    -11585, -10394, -9102, -7723, -6270, -4756, -3196, -1606,
    0, 1606, 3196, 4756, 6270, 7723, 9102, 10394,
    11585, 12665, 13623, 14449, 15137, 15679, 16069, 16305,
};

static const int16_t SIN_Q14[BLOUB_PROFILE_SAMPLES] = {
    0, 1606, 3196, 4756, 6270, 7723, 9102, 10394,
    11585, 12665, 13623, 14449, 15137, 15679, 16069, 16305,
    16384, 16305, 16069, 15679, 15137, 14449, 13623, 12665,
    11585, 10394, 9102, 7723, 6270, 4756, 3196, 1606,
    0, -1606, -3196, -4756, -6270, -7723, -9102, -10394,
    -11585, -12665, -13623, -14449, -15137, -15679, -16069, -16305,
    -16384, -16305, -16069, -15679, -15137, -14449, -13623, -12665,
    -11585, -10394, -9102, -7723, -6270, -4756, -3196, -1606,
};

static const uint16_t EGG_Q12[BLOUB_PROFILE_SAMPLES] = {
    3428, 3450, 3480, 3516, 3553, 3594, 3636, 3679,
    3723, 3762, 3804, 3840, 3869, 3893, 3909, 3915,
    3914, 3899, 3877, 3846, 3810, 3765, 3721, 3674,
    3626, 3577, 3533, 3487, 3445, 3410, 3376, 3350,
    3333, 3323, 3319, 3329, 3350, 3384, 3430, 3489,
    3564, 3654, 3756, 3869, 3990, 4105, 4205, 4273,
    4293, 4257, 4184, 4084, 3972, 3858, 3756, 3666,
    3588, 3524, 3478, 3438, 3415, 3405, 3402, 3410,
};

static const uint16_t HEXAGON_Q12[BLOUB_PROFILE_SAMPLES] = {
    3772, 3802, 3867, 3976, 4089, 4120, 4053, 3917,
    3805, 3737, 3706, 3710, 3751, 3829, 3949, 4044,
    4048, 3959, 3824, 3729, 3673, 3653, 3668, 3719,
    3806, 3937, 4022, 4019, 3928, 3802, 3723, 3677,
    3672, 3697, 3764, 3866, 4005, 4092, 4081, 3978,
    3866, 3799, 3767, 3771, 3813, 3892, 4014, 4146,
    4189, 4125, 3994, 3895, 3836, 3816, 3830, 3885,
    3978, 4107, 4183, 4159, 4040, 3910, 3829, 3781,
};

static const uint16_t TRIANGLE_Q12[BLOUB_PROFILE_SAMPLES] = {
    3203, 3363, 3583, 3867, 4187, 4489, 4670, 4645,
    4427, 4115, 3795, 3524, 3319, 3166, 3052, 2979,
    2929, 2916, 2928, 2968, 3042, 3146, 3292, 3489,
    3747, 4045, 4335, 4536, 4550, 4369, 4071, 3754,
    3474, 3256, 3095, 2974, 2890, 2836, 2809, 2814,
    2842, 2902, 2992, 3119, 3293, 3521, 3814, 4134,
    4420, 4576, 4528, 4301, 4005, 3707, 3461, 3273,
    3136, 3036, 2973, 2933, 2927, 2951, 3002, 3083,
};

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    return value < low ? low : (value > high ? high : value);
}

static int32_t ping_pong(uint32_t elapsed_ms, uint32_t period_ms)
{
    uint32_t phase = elapsed_ms % period_ms;
    uint32_t half = period_ms / 2U;
    return phase <= half ? (int32_t)(phase * 1000U / half)
                         : (int32_t)((period_ms - phase) * 1000U / (period_ms - half));
}

static int32_t wander(uint32_t elapsed_ms, uint32_t period_ms, int32_t amplitude)
{
    return ((ping_pong(elapsed_ms, period_ms) * 2 - 1000) * amplitude) / 1000;
}

static int32_t ease_out_cubic(int32_t value)
{
    int64_t inverse = 1000 - clamp_i32(value, 0, 1000);
    return 1000 - (int32_t)(inverse * inverse * inverse / 1000000);
}

static int32_t ease_out_quint(int32_t value)
{
    int64_t inverse = 1000 - clamp_i32(value, 0, 1000);
    int64_t square = inverse * inverse / 1000;
    int64_t fourth = square * square / 1000;
    return 1000 - (int32_t)(fourth * inverse / 1000);
}

static int32_t ease_in_out_cubic(int32_t value)
{
    value = clamp_i32(value, 0, 1000);
    if (value < 500) {
        return (int32_t)(4LL * value * value * value / 1000000);
    }
    int32_t inverse = 1000 - value;
    return 1000 - (int32_t)(4LL * inverse * inverse * inverse / 1000000);
}

static int32_t lerp_i32(int32_t from, int32_t to, int32_t mix)
{
    return from + (to - from) * mix / 1000;
}

static int32_t blink_open(uint32_t elapsed_ms)
{
    uint32_t phase = elapsed_ms % 3200U;
    if (phase < 1600U || phase > 1780U) {
        return 1000;
    }
    phase -= 1600U;
    return phase <= 70U ? 1000 - (int32_t)(phase * 900U / 70U)
                        : 100 + (int32_t)((phase - 70U) * 900U / 110U);
}

static uint8_t phase_wrap(int32_t phase)
{
    phase %= BLOUB_PROFILE_SAMPLES;
    if (phase < 0) {
        phase += BLOUB_PROFILE_SAMPLES;
    }
    return (uint8_t)phase;
}

static uint16_t phase_wrap_q8(int32_t phase_q8)
{
    phase_q8 %= BLOUB_PROFILE_SAMPLES << 8;
    if (phase_q8 < 0) {
        phase_q8 += BLOUB_PROFILE_SAMPLES << 8;
    }
    return (uint16_t)phase_q8;
}

void bloub_trig_q14(uint8_t phase, int16_t *cosine, int16_t *sine)
{
    phase &= BLOUB_PROFILE_SAMPLES - 1U;
    if (cosine) *cosine = COS_Q14[phase];
    if (sine) *sine = SIN_Q14[phase];
}

uint16_t bloub_zero_crossing_q8(int16_t from, int16_t to)
{
    uint32_t from_magnitude = from < 0 ? (uint32_t)(-(int32_t)from) : (uint32_t)from;
    uint32_t to_magnitude = to < 0 ? (uint32_t)(-(int32_t)to) : (uint32_t)to;
    uint32_t total = from_magnitude + to_magnitude;
    return total ? (uint16_t)((from_magnitude * 256U + total / 2U) / total) : 128U;
}

static void place_silhouette(bloub_frame_t *frame, const uint16_t *profile,
                             int32_t profile_mix, int center_x, int center_y,
                             int scale, uint8_t rotation, uint8_t opacity)
{
    int16_t rc = COS_Q14[rotation & 63U];
    int16_t rs = SIN_Q14[rotation & 63U];
    frame->body_center_x = (int16_t)center_x;
    frame->body_center_y = (int16_t)center_y;
    frame->body_opacity = opacity;

    for (size_t i = 0; i < BLOUB_PROFILE_SAMPLES; i++) {
        int32_t target = profile ? profile[i] : 4096;
        int32_t radius = 4096 + (target - 4096) * profile_mix / 1000;
        int32_t x = (int32_t)((int64_t)COS_Q14[i] * radius * scale / (1LL << 26));
        int32_t y = (int32_t)((int64_t)SIN_Q14[i] * radius * scale / (1LL << 26));
        int32_t xr = ((int32_t)x * rc - (int32_t)y * rs) >> 14;
        int32_t yr = ((int32_t)x * rs + (int32_t)y * rc) >> 14;
        frame->body_points[i].x = (int16_t)(center_x + xr);
        frame->body_points[i].y = (int16_t)(center_y + yr);
    }
}

static void place_ellipse(bloub_frame_t *frame, int center_x, int center_y,
                          int radius_x, int radius_y, uint8_t rotation)
{
    int16_t rc = COS_Q14[rotation & 63U];
    int16_t rs = SIN_Q14[rotation & 63U];
    frame->body_center_x = (int16_t)center_x;
    frame->body_center_y = (int16_t)center_y;
    frame->body_opacity = 255;
    for (size_t i = 0; i < BLOUB_PROFILE_SAMPLES; i++) {
        int32_t x = (int32_t)COS_Q14[i] * radius_x >> 14;
        int32_t y = (int32_t)SIN_Q14[i] * radius_y >> 14;
        frame->body_points[i].x = (int16_t)(center_x + ((x * rc - y * rs) >> 14));
        frame->body_points[i].y = (int16_t)(center_y + ((x * rs + y * rc) >> 14));
    }
}

static void place_eye(bloub_eye_frame_t *eye, int center_x, int center_y,
                      int width, int height, int rotation, uint8_t opacity)
{
    eye->x = (int16_t)(center_x - width / 2);
    eye->y = (int16_t)(center_y - height / 2);
    eye->width = (int16_t)width;
    eye->height = (int16_t)height;
    eye->rotation = (int16_t)rotation;
    eye->opacity = opacity;
}

static void place_idle_eyes(bloub_frame_t *frame, uint32_t elapsed_ms, uint8_t opacity)
{
    int dx = wander(elapsed_ms + 300U, 4400U, 3);
    int dy = wander(elapsed_ms + 900U, 5700U, 2);
    int open = blink_open(elapsed_ms);
    place_eye(&frame->eyes[0], 126 + dx, 83 + dy, 13, 3 + 27 * open / 1000, -260, opacity);
    place_eye(&frame->eyes[1], 156 + dx, 75 + dy, 9, 3 + 27 * open / 1000, -260, opacity);
}

static void add_dot(bloub_frame_t *frame, int x, int y, int diameter,
                    uint8_t opacity, bloub_color_t color, bool behind)
{
    if (frame->dot_count >= BLOUB_MAX_DOTS) return;
    bloub_circle_frame_t *dot = &frame->dots[frame->dot_count++];
    *dot = (bloub_circle_frame_t){
        .x = (int16_t)(x - diameter / 2), .y = (int16_t)(y - diameter / 2),
        .diameter = (int16_t)diameter, .opacity = opacity,
        .color = (uint8_t)color, .behind = behind,
    };
}

static void add_arc(bloub_frame_t *frame, int32_t cx_q8, int32_t cy_q8, int rx, int ry,
                    uint8_t tilt, uint16_t phase_q8, uint8_t sweep, uint8_t width,
                    uint8_t opacity, uint16_t hue, uint16_t hue_span,
                    bool depth_sorted)
{
    if (frame->arc_count >= BLOUB_MAX_ARCS) return;
    bloub_arc_frame_t *arc = &frame->arcs[frame->arc_count++];
    *arc = (bloub_arc_frame_t){
        .center_x_q8 = cx_q8, .center_y_q8 = cy_q8,
        .radius_x = (int16_t)rx, .radius_y = (int16_t)ry,
        .tilt = tilt, .phase_q8 = phase_q8, .sweep = sweep, .width = width,
        .opacity = opacity, .hue = hue, .hue_span = hue_span,
        .depth_sorted = depth_sorted,
    };
}

static void sample_idle(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    place_silhouette(frame, NULL, 0, STAGE_CENTER, STAGE_CENTER,
                     BODY_RADIUS + wander(elapsed_ms, 2800U, 1), 0, 255);
    place_idle_eyes(frame, elapsed_ms, 255);
}

static int32_t dot_pulse(uint32_t elapsed_ms, uint32_t delay_ms)
{
    uint32_t phase = (elapsed_ms + 1500U - delay_ms % 1500U) % 1500U;
    if (phase >= 750U) return 0;
    int32_t ramp = phase < 375U ? (int32_t)(phase * 1000U / 375U)
                                  : (int32_t)((750U - phase) * 1000U / 375U);
    return ease_out_cubic(ramp);
}

static void sample_thinking(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    int32_t left = dot_pulse(elapsed_ms, 0);
    int32_t middle = dot_pulse(elapsed_ms, 500);
    int32_t right = dot_pulse(elapsed_ms, 1000);
    int emerge = 30 + 70 * ease_out_cubic((int32_t)elapsed_ms * 1000 / 300) / 1000;
    place_silhouette(frame, NULL, 0, STAGE_CENTER - 1, STAGE_CENTER,
                     12 + 3 * middle / 1000, 0, 255);
    add_dot(frame, STAGE_CENTER - 40 * emerge / 100, STAGE_CENTER,
            24 + 6 * left / 1000, (uint8_t)(140 + 115 * left / 1000),
            BLOUB_COLOR_INK, true);
    add_dot(frame, STAGE_CENTER + 38 * emerge / 100, STAGE_CENTER,
            24 + 6 * right / 1000, (uint8_t)(140 + 115 * right / 1000),
            BLOUB_COLOR_INK, true);
}

static void sample_wink(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    place_silhouette(frame, NULL, 0, STAGE_CENTER, STAGE_CENTER, BODY_RADIUS, 0, 255);
    place_eye(&frame->eyes[0], 126, 84, 17, 33, -210, 255);
    place_eye(&frame->eyes[1], 155, 80, 32, 6, -100, 255);
    (void)elapsed_ms;
}

static void sample_wide(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    place_silhouette(frame, NULL, 0, STAGE_CENTER, STAGE_CENTER, BODY_RADIUS, 0, 255);
    place_eye(&frame->eyes[0], 124, 82, 25, 57, -160, 255);
    place_eye(&frame->eyes[1], 155, 73, 18, 49, -160, 255);
    (void)elapsed_ms;
}

static void sample_alert(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    int32_t p = ease_in_out_cubic((int32_t)elapsed_ms * 1000 / 1500);
    int32_t back = elapsed_ms > 1600U
                       ? clamp_i32((int32_t)(elapsed_ms - 1600U) * 1000 / 400, 0, 1000)
                       : 0;
    int x = lerp_i32(STAGE_CENTER - 6, STAGE_CENTER + 52, p);
    x = lerp_i32(x, STAGE_CENTER + 7, back);
    int buzz = wander(elapsed_ms, 400U, 1);
    place_ellipse(frame, x, STAGE_CENTER - 23 - buzz, 10, 39, 3);
    add_dot(frame, x - 13, STAGE_CENTER + 25 + buzz * 2, 18, 255,
            BLOUB_COLOR_INK, false);
}

static void sample_notify(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    int32_t p = clamp_i32((int32_t)elapsed_ms * 1000 / 450, 0, 1000);
    int pop = p < 1000 ? 11 + (int)(3LL * (1000 - (p - 500) * (p - 500) / 250) / 1000) : 11;
    place_silhouette(frame, NULL, 0, STAGE_CENTER, STAGE_CENTER, BODY_RADIUS, 0, 255);
    place_eye(&frame->eyes[0], 122, 92, 27, 36, -220, 255);
    place_eye(&frame->eyes[1], 151, 84, 19, 34, -220, 255);
    add_dot(frame, 165, 64, 2 * (pop + 4), 255, BLOUB_COLOR_PAPER, false);
    add_dot(frame, 165, 64, 2 * pop, 255, BLOUB_COLOR_NOTIFY, false);
}

static void sample_exclaim(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    place_ellipse(frame, STAGE_CENTER, STAGE_CENTER - 25, 11, 37, 0);
    add_dot(frame, STAGE_CENTER - 1, STAGE_CENTER + 38, 17, 255, BLOUB_COLOR_INK, false);
    (void)elapsed_ms;
}

static void sample_sleep(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    int bounce = wander(elapsed_ms, 600U, 14);
    place_silhouette(frame, NULL, 0, STAGE_CENTER, STAGE_CENTER + 8 + bounce, 11, 0, 255);
}

static void sample_profile(uint32_t elapsed_ms, bloub_frame_t *frame,
                           const uint16_t *profile, bool egg)
{
    int open = blink_open(elapsed_ms);
    place_silhouette(frame, profile, 1000, STAGE_CENTER, STAGE_CENTER, BODY_RADIUS, 0, 255);
    if (egg) {
        place_eye(&frame->eyes[0], 122, 85, 11, 3 + 24 * open / 1000, -260, 255);
        place_eye(&frame->eyes[1], 145, 77, 9, 3 + 24 * open / 1000, -260, 255);
    } else {
        place_eye(&frame->eyes[0], 123, 87, 12, 3 + 25 * open / 1000, -226, 255);
        place_eye(&frame->eyes[1], 151, 80, 10, 3 + 25 * open / 1000, -226, 255);
    }
}

static void sample_play(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    /* The triangle orbits 0.213 body radii below the origin even when unrotated. */
    place_silhouette(frame, TRIANGLE_Q12, 1000, STAGE_CENTER,
                     STAGE_CENTER + 15, BODY_RADIUS, 0, 255);
    place_eye(&frame->eyes[0], STAGE_CENTER - 1, STAGE_CENTER + 7, 14, 24, -60, 255);
    place_eye(&frame->eyes[1], STAGE_CENTER + 29, STAGE_CENTER + 5, 12, 24, -60, 255);
    int32_t fade_in = clamp_i32((int32_t)elapsed_ms * 255 / 350, 0, 255);
    int32_t fade_out = clamp_i32((2200 - (int32_t)elapsed_ms) * 255 / 500, 0, 255);
    uint8_t opacity = (uint8_t)(fade_in < fade_out ? fade_in : fade_out);
    static const uint8_t tilt[] = { 58, 58, 59, 59 };
    static const uint8_t radius_x[] = { 55, 70, 84, 98 };
    static const uint8_t radius_y[] = { 3, 5, 8, 11 };
    for (uint8_t i = 0; i < 4; i++) {
        int32_t phase_q8 = 48 * 256 + (int32_t)(((int64_t)elapsed_ms * 370 * 256 +
                                      (int64_t)i * 6110 * 256 + 5000) / 10000);
        add_arc(frame, STAGE_CENTER * 256, (STAGE_CENTER + 15) * 256,
                radius_x[i], radius_y[i], tilt[i],
                phase_wrap_q8(phase_q8), 26, 4, opacity,
                95 + i * 62, 100, true);
    }
}

static void sample_orbit(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    int32_t ramp = ease_in_out_cubic((int32_t)elapsed_ms * 1000 / 350);
    int32_t turns = -(int32_t)(elapsed_ms * 80U / 1000U);
    uint8_t rotation = phase_wrap(turns * ramp / 1000);
    int32_t back = ease_in_out_cubic(((int32_t)elapsed_ms - 1600) * 1000 / 900);
    int16_t c = COS_Q14[rotation];
    int16_t s = SIN_Q14[rotation];
    int cx = STAGE_CENTER - (((int32_t)s * 15 >> 14) * (1000 - back) / 1000);
    int cy = STAGE_CENTER + (((int32_t)c * 15 >> 14) * (1000 - back) / 1000);
    place_silhouette(frame, TRIANGLE_Q12, 1000 - back, cx, cy, BODY_RADIUS, rotation, 255);
    int orbit_x = (int32_t)SIN_Q14[phase_wrap((int32_t)elapsed_ms * 66 / 1000)] * 18 >> 14;
    place_eye(&frame->eyes[0], 126 + orbit_x * (1000 - back) / 1000, 83, 12, 25, -130, 255);
    place_eye(&frame->eyes[1], 153 + orbit_x * (1000 - back) / 1000, 77, 9, 24, -130, 255);
    for (uint8_t i = 0; i < 6; i++) {
        int32_t enter = clamp_i32((int32_t)elapsed_ms - i * 130, 0, 300) * 255 / 300;
        int32_t leave = clamp_i32(3600 - (int32_t)elapsed_ms, 0, 900) * 255 / 900;
        add_arc(frame, STAGE_CENTER * 256, (STAGE_CENTER + 7) * 256,
                94 + i, 8 + i * 5, (uint8_t)(i * 6 + 2),
                phase_wrap_q8((int32_t)((int64_t)elapsed_ms * (190 + i * 7) * 256 / 1000) +
                              i * 9 * 256),
                42 + (i & 3), 4, (uint8_t)(enter < leave ? enter : leave),
                i * 60, 90, true);
    }
}

static void sample_burst(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    int32_t collapse = ease_out_quint((int32_t)elapsed_ms * 1000 / 700);
    int radius = BODY_RADIUS - 59 * collapse / 1000;
    int32_t regrow = ease_out_quint(((int32_t)elapsed_ms - 1700) * 1000 / 700);
    radius = lerp_i32(radius, BODY_RADIUS, regrow);
    place_silhouette(frame, NULL, 0, STAGE_CENTER, STAGE_CENTER, radius, 0, 255);
    static const uint8_t seeds[] = { 3, 14, 27, 39, 52 };
    for (uint8_t i = 0; i < 5; i++) {
        int32_t age = (int32_t)elapsed_ms - i * 200;
        if (age < 0 || age > 620) continue;
        int rho = 45 - age * 28 / 620;
        uint8_t phase = phase_wrap(seeds[i] + age * 18 / 620);
        int x = STAGE_CENTER + ((int32_t)COS_Q14[phase] * rho >> 14);
        int y = STAGE_CENTER + ((int32_t)SIN_Q14[phase] * rho >> 14);
        add_dot(frame, x, y, 6 + age * 3 / 620, 255,
                (bloub_color_t)(BLOUB_COLOR_RED + i), true);
    }
    place_idle_eyes(frame, elapsed_ms,
                    (uint8_t)clamp_i32(((int32_t)elapsed_ms - 1850) * 255 / 400, 0, 255));
}

static void sample_comet(uint32_t elapsed_ms, bloub_frame_t *frame)
{
    int32_t collapse = ease_out_quint((int32_t)elapsed_ms * 1000 / 550);
    int radius = BODY_RADIUS - 62 * collapse / 1000;
    int32_t regrow = ease_out_quint(((int32_t)elapsed_ms - 1850) * 1000 / 600);
    radius = lerp_i32(radius, BODY_RADIUS, regrow);
    int wobble = (int32_t)SIN_Q14[phase_wrap((int32_t)elapsed_ms * 32 / 1700)] * 3 >> 14;
    place_silhouette(frame, NULL, 0, STAGE_CENTER, STAGE_CENTER + wobble, radius, 0, 255);
    int32_t fade_in = clamp_i32(((int32_t)elapsed_ms - 150) * 255 / 250, 0, 255);
    int32_t fade_out = clamp_i32((1950 - (int32_t)elapsed_ms) * 255 / 300, 0, 255);
    uint8_t opacity = (uint8_t)(fade_in < fade_out ? fade_in : fade_out);
    for (uint8_t i = 0; i < 4; i++) {
        add_arc(frame, STAGE_CENTER * 256, STAGE_CENTER * 256, 61 + i, 9 + i, 6,
                phase_wrap_q8((int32_t)((int64_t)elapsed_ms * 37 * 256 / 1000) - i * 3 * 256),
                22, 6, opacity, i * 85, 80, true);
    }
    place_idle_eyes(frame, elapsed_ms,
                    (uint8_t)clamp_i32(((int32_t)elapsed_ms - 2000) * 255 / 350, 0, 255));
}

const char *bloub_state_name(bloub_state_t state)
{
    static const char *const names[] = {
        "IDLE", "THINKING", "WINK", "WIDE", "ALERT", "NOTIFY", "EXCLAIM",
        "SLEEP", "EGG", "HEXAGON", "PLAY", "ORBIT", "BURST", "COMET",
    };
    return state >= 0 && state < BLOUB_STATE_COUNT ? names[state] : "UNKNOWN";
}

uint32_t bloub_state_duration_ms(bloub_state_t state)
{
    static const uint16_t durations[] = {
        2400, 2600, 1600, 1800, 2400, 2200, 2000,
        2400, 1800, 1600, 2200, 3400, 2600, 2400,
    };
    return state >= 0 && state < BLOUB_STATE_COUNT ? durations[state] : durations[0];
}

uint32_t bloub_state_morph_ms(bloub_state_t state)
{
    static const uint16_t durations[] = {
        450, 400, 300, 550, 450, 500, 450,
        500, 400, 400, 500, 600, 400, 450,
    };
    return state >= 0 && state < BLOUB_STATE_COUNT ? durations[state] : durations[0];
}

void bloub_sample(bloub_state_t state, uint32_t elapsed_ms, bloub_frame_t *frame)
{
    if (!frame) return;
    memset(frame, 0, sizeof(*frame));
    switch (state) {
    case BLOUB_STATE_THINKING: sample_thinking(elapsed_ms, frame); break;
    case BLOUB_STATE_WINK: sample_wink(elapsed_ms, frame); break;
    case BLOUB_STATE_WIDE: sample_wide(elapsed_ms, frame); break;
    case BLOUB_STATE_ALERT: sample_alert(elapsed_ms, frame); break;
    case BLOUB_STATE_NOTIFY: sample_notify(elapsed_ms, frame); break;
    case BLOUB_STATE_EXCLAIM: sample_exclaim(elapsed_ms, frame); break;
    case BLOUB_STATE_SLEEP: sample_sleep(elapsed_ms, frame); break;
    case BLOUB_STATE_EGG: sample_profile(elapsed_ms, frame, EGG_Q12, true); break;
    case BLOUB_STATE_HEXAGON: sample_profile(elapsed_ms, frame, HEXAGON_Q12, false); break;
    case BLOUB_STATE_PLAY: sample_play(elapsed_ms, frame); break;
    case BLOUB_STATE_ORBIT: sample_orbit(elapsed_ms, frame); break;
    case BLOUB_STATE_BURST: sample_burst(elapsed_ms, frame); break;
    case BLOUB_STATE_COMET: sample_comet(elapsed_ms, frame); break;
    case BLOUB_STATE_IDLE:
    default: sample_idle(elapsed_ms, frame); break;
    }
}

void bloub_blend(const bloub_frame_t *from, const bloub_frame_t *to,
                 uint16_t mix, bloub_frame_t *frame)
{
    if (!from || !to || !frame) return;
    int32_t eased = ease_out_quint(mix > 1000U ? 1000 : mix);
    *frame = *to;
    for (size_t i = 0; i < BLOUB_PROFILE_SAMPLES; i++) {
        frame->body_points[i].x = (int16_t)lerp_i32(from->body_points[i].x, to->body_points[i].x, eased);
        frame->body_points[i].y = (int16_t)lerp_i32(from->body_points[i].y, to->body_points[i].y, eased);
    }
    frame->body_center_x = (int16_t)lerp_i32(from->body_center_x, to->body_center_x, eased);
    frame->body_center_y = (int16_t)lerp_i32(from->body_center_y, to->body_center_y, eased);
    frame->body_opacity = (uint8_t)lerp_i32(from->body_opacity, to->body_opacity, eased);
    for (size_t i = 0; i < 2; i++) {
        frame->eyes[i].x = (int16_t)lerp_i32(from->eyes[i].x, to->eyes[i].x, eased);
        frame->eyes[i].y = (int16_t)lerp_i32(from->eyes[i].y, to->eyes[i].y, eased);
        frame->eyes[i].width = (int16_t)lerp_i32(from->eyes[i].width, to->eyes[i].width, eased);
        frame->eyes[i].height = (int16_t)lerp_i32(from->eyes[i].height, to->eyes[i].height, eased);
        frame->eyes[i].rotation = (int16_t)lerp_i32(from->eyes[i].rotation, to->eyes[i].rotation, eased);
        frame->eyes[i].opacity = (uint8_t)lerp_i32(from->eyes[i].opacity, to->eyes[i].opacity, eased);
    }
}

bool bloub_scanline_span(const bloub_point_t *points, size_t count, int16_t y,
                         int16_t *left, int16_t *right)
{
    if (!points || count < 3 || !left || !right) return false;
    int32_t min_x = INT32_MAX;
    int32_t max_x = INT32_MIN;
    for (size_t i = 0; i < count; i++) {
        const bloub_point_t *a = &points[i];
        const bloub_point_t *b = &points[(i + 1U) % count];
        if (!((a->y <= y && b->y > y) || (b->y <= y && a->y > y))) continue;
        int32_t x = a->x + (int32_t)(y - a->y) * (b->x - a->x) / (b->y - a->y);
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
    }
    if (min_x > max_x) return false;
    *left = (int16_t)min_x;
    *right = (int16_t)max_x;
    return true;
}
