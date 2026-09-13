#include <assert.h>
#include <string.h>

#include "bloub_math.h"

int main(void)
{
    static const char *const names[] = {
        "IDLE", "THINKING", "WINK", "WIDE", "ALERT", "NOTIFY", "EXCLAIM",
        "SLEEP", "EGG", "HEXAGON", "PLAY", "ORBIT", "BURST", "COMET",
    };
    static const uint32_t durations[] = {
        2400, 2600, 1600, 1800, 2400, 2200, 2000,
        2400, 1800, 1600, 2200, 3400, 2600, 2400,
    };
    bloub_frame_t frame;

    assert(BLOUB_STATE_COUNT == 14);
    for (int state = 0; state < BLOUB_STATE_COUNT; state++) {
        assert(strcmp(bloub_state_name((bloub_state_t)state), names[state]) == 0);
        assert(bloub_state_duration_ms((bloub_state_t)state) == durations[state]);
        assert(bloub_state_morph_ms((bloub_state_t)state) >= 300);
        assert(bloub_state_morph_ms((bloub_state_t)state) <= 600);
        bloub_sample((bloub_state_t)state, durations[state] / 2, &frame);
        assert(frame.body_opacity == 255);
        assert(frame.dot_count <= BLOUB_MAX_DOTS);
        assert(frame.arc_count <= BLOUB_MAX_ARCS);
        for (size_t i = 0; i < BLOUB_PROFILE_SAMPLES; i++) {
            assert(frame.body_points[i].x >= 0 && frame.body_points[i].x < BLOUB_STAGE_SIZE);
            assert(frame.body_points[i].y >= 0 && frame.body_points[i].y < BLOUB_STAGE_SIZE);
        }
    }

    assert(strcmp(bloub_state_name((bloub_state_t)99), "UNKNOWN") == 0);
    assert(bloub_zero_crossing_q8(-100, 300) == 64);
    assert(bloub_zero_crossing_q8(-300, 100) == 192);
    assert(bloub_zero_crossing_q8(0, 0) == 128);

    bloub_sample(BLOUB_STATE_IDLE, 0, &frame);
    assert(frame.body_center_x == BLOUB_STAGE_SIZE / 2);
    assert(frame.eyes[0].opacity == 255);
    int16_t left;
    int16_t right;
    assert(bloub_scanline_span(frame.body_points, BLOUB_PROFILE_SAMPLES,
                               BLOUB_STAGE_SIZE / 2, &left, &right));
    assert(right - left >= 138);

    /* One span per row is the seam-free replacement for the old triangle fan. */
    for (int16_t y = 48; y <= 176; y++) {
        assert(bloub_scanline_span(frame.body_points, BLOUB_PROFILE_SAMPLES, y, &left, &right));
        assert(left <= right);
    }

    bloub_sample(BLOUB_STATE_THINKING, 1100, &frame);
    assert(frame.dot_count == 2);
    assert(frame.eyes[0].opacity == 0);

    bloub_sample(BLOUB_STATE_WINK, 800, &frame);
    assert(frame.eyes[1].width > frame.eyes[1].height);

    bloub_sample(BLOUB_STATE_WIDE, 800, &frame);
    assert(frame.eyes[0].height > 50);

    bloub_sample(BLOUB_STATE_ALERT, 750, &frame);
    assert(frame.dot_count == 1);
    assert(frame.eyes[0].opacity == 0);

    bloub_sample(BLOUB_STATE_NOTIFY, 450, &frame);
    assert(frame.dot_count == 2);
    assert(frame.dots[0].color == BLOUB_COLOR_PAPER);
    assert(frame.dots[1].color == BLOUB_COLOR_NOTIFY);

    bloub_sample(BLOUB_STATE_EXCLAIM, 800, &frame);
    assert(frame.dot_count == 1);

    bloub_sample(BLOUB_STATE_PLAY, 900, &frame);
    assert(frame.arc_count == 4);
    assert(frame.body_center_x == BLOUB_STAGE_SIZE / 2);
    assert(frame.body_center_y == BLOUB_STAGE_SIZE / 2 + 15);
    assert(frame.eyes[0].x + frame.eyes[0].width / 2 == BLOUB_STAGE_SIZE / 2 - 1);
    assert(frame.eyes[0].y + frame.eyes[0].height / 2 == BLOUB_STAGE_SIZE / 2 + 7);
    assert(frame.eyes[1].x + frame.eyes[1].width / 2 == BLOUB_STAGE_SIZE / 2 + 29);
    assert(frame.eyes[1].y + frame.eyes[1].height / 2 == BLOUB_STAGE_SIZE / 2 + 5);
    assert(frame.eyes[0].rotation == -60);
    assert(frame.eyes[1].rotation == -60);
    static const uint8_t play_tilt[] = { 58, 58, 59, 59 };
    static const uint16_t play_phase_q8[] = { 4429, 4585, 4742, 4898 };
    static const int16_t play_radius_x[] = { 55, 70, 84, 98 };
    static const int16_t play_radius_y[] = { 3, 5, 8, 11 };
    for (size_t i = 0; i < frame.arc_count; i++) {
        assert(frame.arcs[i].center_x_q8 == (BLOUB_STAGE_SIZE / 2) * 256);
        assert(frame.arcs[i].center_y_q8 == (BLOUB_STAGE_SIZE / 2 + 15) * 256);
        assert(frame.arcs[i].tilt == play_tilt[i]);
        assert(frame.arcs[i].phase_q8 == play_phase_q8[i]);
        assert(frame.arcs[i].radius_x == play_radius_x[i]);
        assert(frame.arcs[i].radius_y == play_radius_y[i]);
        assert(frame.arcs[i].hue == 95 + i * 62);
        assert(frame.arcs[i].hue_span == 100);
        assert(frame.arcs[i].depth_sorted);
    }
    bloub_frame_t play_next;
    bloub_sample(BLOUB_STATE_PLAY, 925, &play_next);
    assert(play_next.arcs[0].phase_q8 - frame.arcs[0].phase_q8 == 237);
    assert(play_next.arcs[0].center_x_q8 == frame.arcs[0].center_x_q8);
    bloub_sample(BLOUB_STATE_PLAY, 1730, &play_next);
    assert((play_next.arcs[0].phase_q8 + 16384 - 48 * 256) % 16384 < 8);

    bloub_sample(BLOUB_STATE_ORBIT, 1200, &frame);
    assert(frame.arc_count == 6);

    bloub_sample(BLOUB_STATE_BURST, 450, &frame);
    assert(frame.dot_count == 3);

    bloub_sample(BLOUB_STATE_COMET, 1150, &frame);
    assert(frame.arc_count == 4);

    bloub_frame_t idle;
    bloub_frame_t egg;
    bloub_frame_t blended;
    bloub_sample(BLOUB_STATE_IDLE, 0, &idle);
    bloub_sample(BLOUB_STATE_EGG, 0, &egg);
    bloub_blend(&idle, &egg, 0, &blended);
    assert(blended.body_points[0].x == idle.body_points[0].x);
    bloub_blend(&idle, &egg, 1000, &blended);
    assert(blended.body_points[0].x == egg.body_points[0].x);
    bloub_blend(&idle, &egg, 500, &blended);
    assert(blended.body_points[0].x < idle.body_points[0].x);
    assert(blended.body_points[0].x > egg.body_points[0].x);

    memset(&frame, 0x7f, sizeof(frame));
    bloub_sample((bloub_state_t)99, 0, &frame);
    assert(frame.body_opacity == 255);
    assert(frame.dot_count == 0);
    return 0;
}
