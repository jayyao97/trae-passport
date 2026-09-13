// Full-screen Bloub player with a small antialiased RGB565 rasterizer.
#include "demo.h"
#include "bloub_math.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FRAME_PERIOD_MS 25
/* Warm paper color outside the panel's near-white compression range. */
#define BACKGROUND_COLOR 0xEFD7A5
#define EYE_COLOR 0xFFFFFF
#define INK_COLOR 0x0A0A0C
#define OUTSIDE_COLOR 0x000000
#define DISPLAY_CORNER_RADIUS 24
#define STAGE_X ((BSP_LCD_W - BLOUB_STAGE_SIZE) / 2)
#define STAGE_Y ((BSP_LCD_H - BLOUB_STAGE_SIZE) / 2)
#define STAGE_PIXELS (BLOUB_STAGE_SIZE * BLOUB_STAGE_SIZE)

#define EVENT_PREVIOUS (1U << 0)
#define EVENT_NEXT (1U << 1)
#define EVENT_AUTOPLAY (1U << 2)

static const char *TAG = "bloub";
static uint16_t *s_buffers[2];
static uint8_t s_buffer_count;
static SemaphoreHandle_t s_transfer_done;
static SemaphoreHandle_t s_task_stopped;
static TaskHandle_t s_task;
static volatile bool s_running;
static uint32_t s_pending_events;
static bool s_autoplay;
static bloub_state_t s_state;
static bloub_frame_t s_frame;
static bloub_frame_t s_transition_from;
static bool s_transitioning;
static uint32_t s_state_started_at;

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xF800U) | ((rgb >> 5) & 0x07E0U) | ((rgb >> 3) & 0x001FU));
}

static uint16_t swap16(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint16_t background_pixel(int x, int y)
{
    const int radius_q8 = DISPLAY_CORNER_RADIUS << 8;
    int dx_q8 = 0;
    int dy_q8 = 0;
    int pixel_x_q8 = (x << 8) + 128;
    int pixel_y_q8 = (y << 8) + 128;
    if (pixel_x_q8 < radius_q8) dx_q8 = radius_q8 - pixel_x_q8;
    else if (pixel_x_q8 > (BSP_LCD_W << 8) - radius_q8) {
        dx_q8 = pixel_x_q8 - ((BSP_LCD_W << 8) - radius_q8);
    }
    if (pixel_y_q8 < radius_q8) dy_q8 = radius_q8 - pixel_y_q8;
    else if (pixel_y_q8 > (BSP_LCD_H << 8) - radius_q8) {
        dy_q8 = pixel_y_q8 - ((BSP_LCD_H << 8) - radius_q8);
    }
    if (dx_q8 == 0 || dy_q8 == 0) return swap16(rgb565(BACKGROUND_COLOR));

    int64_t distance_sq = (int64_t)dx_q8 * dx_q8 + (int64_t)dy_q8 * dy_q8;
    int32_t inner_q8 = radius_q8 - 256;
    int64_t inner_sq = (int64_t)inner_q8 * inner_q8;
    int64_t outer_sq = (int64_t)radius_q8 * radius_q8;
    if (distance_sq <= inner_sq) return swap16(rgb565(BACKGROUND_COLOR));
    if (distance_sq >= outer_sq) return swap16(rgb565(OUTSIDE_COLOR));

    uint32_t alpha = (uint32_t)((outer_sq - distance_sq) * 255 / (outer_sq - inner_sq));
    uint32_t red = ((BACKGROUND_COLOR >> 16) & 0xFFU) * alpha / 255U;
    uint32_t green = ((BACKGROUND_COLOR >> 8) & 0xFFU) * alpha / 255U;
    uint32_t blue = (BACKGROUND_COLOR & 0xFFU) * alpha / 255U;
    return swap16(rgb565(red << 16 | green << 8 | blue));
}

static uint32_t wheel_rgb(uint16_t hue)
{
    hue %= 360;
    uint8_t sector = hue / 60;
    uint8_t offset = hue % 60;
    uint8_t rising = (uint8_t)(105 + 106 * offset / 60);
    uint8_t falling = (uint8_t)(211 - 106 * offset / 60);
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    switch (sector) {
    case 0: red = 211; green = rising; blue = 105; break;
    case 1: red = falling; green = 211; blue = 105; break;
    case 2: red = 105; green = 211; blue = rising; break;
    case 3: red = 105; green = falling; blue = 211; break;
    case 4: red = rising; green = 105; blue = 211; break;
    default: red = 211; green = 105; blue = falling; break;
    }
    return (uint32_t)red << 16 | (uint32_t)green << 8 | blue;
}

static uint32_t dot_rgb(uint8_t color)
{
    static const uint32_t colors[] = {
        [BLOUB_COLOR_INK] = INK_COLOR,
        [BLOUB_COLOR_PAPER] = BACKGROUND_COLOR,
        [BLOUB_COLOR_NOTIFY] = 0x2496E8,
        [BLOUB_COLOR_RED] = 0xF43F5E,
        [BLOUB_COLOR_ORANGE] = 0xFB923C,
        [BLOUB_COLOR_YELLOW] = 0xFACC15,
        [BLOUB_COLOR_GREEN] = 0x34D399,
        [BLOUB_COLOR_CYAN] = 0x22D3EE,
        [BLOUB_COLOR_BLUE] = 0x3B82F6,
        [BLOUB_COLOR_VIOLET] = 0x8B5CF6,
        [BLOUB_COLOR_PINK] = 0xEC4899,
    };
    return color < sizeof(colors) / sizeof(colors[0]) ? colors[color] : INK_COLOR;
}

static void blend_pixel(uint16_t *pixels, int x, int y, uint32_t rgb, uint8_t alpha)
{
    if ((unsigned)x >= BLOUB_STAGE_SIZE || (unsigned)y >= BLOUB_STAGE_SIZE || alpha == 0) return;
    uint16_t *pixel = &pixels[y * BLOUB_STAGE_SIZE + x];
    if (alpha == 255) {
        *pixel = swap16(rgb565(rgb));
        return;
    }
    uint16_t dst = swap16(*pixel);
    uint8_t dr = (uint8_t)((((dst >> 11) & 0x1F) * 527 + 23) >> 6);
    uint8_t dg = (uint8_t)((((dst >> 5) & 0x3F) * 259 + 33) >> 6);
    uint8_t db = (uint8_t)(((dst & 0x1F) * 527 + 23) >> 6);
    uint8_t sr = (uint8_t)(rgb >> 16);
    uint8_t sg = (uint8_t)(rgb >> 8);
    uint8_t sb = (uint8_t)rgb;
    uint16_t inverse = 255U - alpha;
    uint8_t r = (uint8_t)((sr * alpha + dr * inverse + 127) / 255);
    uint8_t g = (uint8_t)((sg * alpha + dg * inverse + 127) / 255);
    uint8_t b = (uint8_t)((sb * alpha + db * inverse + 127) / 255);
    *pixel = swap16(rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | b));
}

static bool body_span_q8(const bloub_point_t *points, int32_t sample_y,
                         int32_t *left, int32_t *right)
{
    int32_t minimum = INT32_MAX;
    int32_t maximum = INT32_MIN;
    uint8_t crossings = 0;
    for (size_t i = 0; i < BLOUB_PROFILE_SAMPLES; i++) {
        const bloub_point_t *a = &points[i];
        const bloub_point_t *b = &points[(i + 1) % BLOUB_PROFILE_SAMPLES];
        int32_t ay = (int32_t)a->y << 8;
        int32_t by = (int32_t)b->y << 8;
        if (!((ay <= sample_y && by > sample_y) || (by <= sample_y && ay > sample_y))) continue;
        int32_t x = ((int32_t)a->x << 8) +
                    (int32_t)(((int64_t)(sample_y - ay) * (b->x - a->x)) / (b->y - a->y));
        if (x < minimum) minimum = x;
        if (x > maximum) maximum = x;
        crossings++;
    }
    if (crossings < 2) return false;
    *left = minimum;
    *right = maximum;
    return true;
}

static void draw_body(uint16_t *pixels)
{
    static const uint8_t offsets[] = { 64, 192 };
    const uint16_t ink = swap16(rgb565(INK_COLOR));
    for (int y = 0; y < BLOUB_STAGE_SIZE; y++) {
        int32_t left[2];
        int32_t right[2];
        int32_t min_x = INT32_MAX;
        int32_t max_x = INT32_MIN;
        bool valid[2];
        for (size_t sample = 0; sample < 2; sample++) {
            valid[sample] = body_span_q8(s_frame.body_points, (y << 8) + offsets[sample],
                                         &left[sample], &right[sample]);
            if (valid[sample]) {
                if (left[sample] < min_x) min_x = left[sample];
                if (right[sample] > max_x) max_x = right[sample];
            }
        }
        if (min_x > max_x) continue;
        int x0 = min_x >> 8;
        int x1 = (max_x + 255) >> 8;
        if (x0 < 0) x0 = 0;
        if (x1 > BLOUB_STAGE_SIZE) x1 = BLOUB_STAGE_SIZE;

        int full_x0 = x1;
        int full_x1 = x1;
        if (valid[0] && valid[1]) {
            int32_t inner_left = left[0] > left[1] ? left[0] : left[1];
            int32_t inner_right = right[0] < right[1] ? right[0] : right[1];
            full_x0 = (inner_left + 255) >> 8;
            full_x1 = inner_right >> 8;
            if (full_x0 < x0) full_x0 = x0;
            if (full_x1 > x1) full_x1 = x1;
            if (full_x1 < full_x0) full_x0 = full_x1 = x1;
        }

        for (int x = x0; x < full_x0; x++) {
            int32_t pixel_left = x << 8;
            int32_t pixel_right = pixel_left + 256;
            uint16_t coverage = 0;
            for (size_t sample = 0; sample < 2; sample++) {
                if (!valid[sample]) continue;
                int32_t lo = left[sample] > pixel_left ? left[sample] : pixel_left;
                int32_t hi = right[sample] < pixel_right ? right[sample] : pixel_right;
                if (hi > lo) coverage += (uint16_t)(hi - lo);
            }
            uint8_t alpha = coverage == 512U ? s_frame.body_opacity
                                             : (uint8_t)((uint32_t)s_frame.body_opacity * coverage / 512U);
            blend_pixel(pixels, x, y, INK_COLOR, alpha);
        }
        for (int x = full_x0; x < full_x1; x++) {
            if (s_frame.body_opacity == 255) pixels[y * BLOUB_STAGE_SIZE + x] = ink;
            else blend_pixel(pixels, x, y, INK_COLOR, s_frame.body_opacity);
        }
        for (int x = full_x1; x < x1; x++) {
            int32_t pixel_left = x << 8;
            int32_t pixel_right = pixel_left + 256;
            uint16_t coverage = 0;
            for (size_t sample = 0; sample < 2; sample++) {
                if (!valid[sample]) continue;
                int32_t lo = left[sample] > pixel_left ? left[sample] : pixel_left;
                int32_t hi = right[sample] < pixel_right ? right[sample] : pixel_right;
                if (hi > lo) coverage += (uint16_t)(hi - lo);
            }
            uint8_t alpha = (uint8_t)((uint32_t)s_frame.body_opacity * coverage / 512U);
            blend_pixel(pixels, x, y, INK_COLOR, alpha);
        }
    }
}

static uint32_t integer_sqrt(uint32_t value)
{
    uint32_t result = 0;
    uint32_t bit = 1U << 30;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint8_t capsule_coverage(int32_t px, int32_t py, int32_t ax, int32_t ay,
                                int32_t bx, int32_t by, int32_t dx, int32_t dy,
                                int32_t length_sq, int32_t inner_radius_sq,
                                int32_t outer_radius_sq, int32_t inner_cross_limit,
                                int32_t outer_cross_limit)
{
    int32_t ex = px - ax;
    int32_t ey = py - ay;
    int32_t projection = ex * dx + ey * dy;
    int32_t metric;
    if (projection <= 0 || length_sq == 0) {
        metric = ex * ex + ey * ey;
        if (metric <= inner_radius_sq) return 2;
        return metric <= outer_radius_sq ? 1 : 0;
    } else if (projection >= length_sq) {
        ex = px - bx;
        ey = py - by;
        metric = ex * ex + ey * ey;
        if (metric <= inner_radius_sq) return 2;
        return metric <= outer_radius_sq ? 1 : 0;
    }
    int32_t cross = ex * dy - ey * dx;
    if (cross < 0) cross = -cross;
    if (cross <= inner_cross_limit) return 2;
    return cross <= outer_cross_limit ? 1 : 0;
}

static void draw_capsule(uint16_t *pixels, int32_t ax, int32_t ay, int32_t bx, int32_t by,
                         int32_t radius_q8, uint32_t rgb, uint8_t opacity)
{
    int pad = (radius_q8 + 255) >> 8;
    int x0 = ((ax < bx ? ax : bx) >> 8) - pad - 1;
    int x1 = ((ax > bx ? ax : bx) >> 8) + pad + 1;
    int y0 = ((ay < by ? ay : by) >> 8) - pad - 1;
    int y1 = ((ay > by ? ay : by) >> 8) + pad + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= BLOUB_STAGE_SIZE) x1 = BLOUB_STAGE_SIZE - 1;
    if (y1 >= BLOUB_STAGE_SIZE) y1 = BLOUB_STAGE_SIZE - 1;
    int32_t inner_radius = radius_q8 > 128 ? radius_q8 - 128 : 0;
    int32_t outer_radius = radius_q8 + 128;
    int32_t dx = bx - ax;
    int32_t dy = by - ay;
    int32_t length_sq = dx * dx + dy * dy;
    int32_t length = (int32_t)integer_sqrt((uint32_t)length_sq);
    int32_t inner_radius_sq = inner_radius * inner_radius;
    int32_t outer_radius_sq = outer_radius * outer_radius;
    int32_t inner_cross_limit = inner_radius * length;
    int32_t outer_cross_limit = outer_radius * length;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int32_t px = (x << 8) + 128;
            int32_t py = (y << 8) + 128;
            uint8_t coverage = capsule_coverage(px, py, ax, ay, bx, by, dx, dy, length_sq,
                                                inner_radius_sq, outer_radius_sq,
                                                inner_cross_limit, outer_cross_limit);
            if (coverage == 2) {
                blend_pixel(pixels, x, y, rgb, opacity);
            } else if (coverage == 1) {
                blend_pixel(pixels, x, y, rgb, (uint8_t)(opacity / 2));
            }
        }
    }
}

static void draw_eye(uint16_t *pixels, const bloub_eye_frame_t *eye)
{
    if (eye->opacity == 0 || eye->width <= 0 || eye->height <= 0) return;
    bool vertical = eye->height > eye->width;
    int thickness = vertical ? eye->width : eye->height;
    int length = vertical ? eye->height : eye->width;
    int half_line_q8 = (length - thickness) * 128;
    int phase = eye->rotation * BLOUB_PROFILE_SAMPLES / 3600 + (vertical ? 16 : 0);
    phase %= BLOUB_PROFILE_SAMPLES;
    if (phase < 0) phase += BLOUB_PROFILE_SAMPLES;
    int16_t cosine;
    int16_t sine;
    bloub_trig_q14((uint8_t)phase, &cosine, &sine);
    int32_t cx = (eye->x * 2 + eye->width) * 128;
    int32_t cy = (eye->y * 2 + eye->height) * 128;
    int32_t ux = (int32_t)cosine * half_line_q8 >> 14;
    int32_t uy = (int32_t)sine * half_line_q8 >> 14;
    draw_capsule(pixels, cx - ux, cy - uy, cx + ux, cy + uy,
                 thickness * 128, EYE_COLOR, eye->opacity);
}

static void draw_circle(uint16_t *pixels, const bloub_circle_frame_t *circle)
{
    if (circle->opacity == 0 || circle->diameter <= 0) return;
    int32_t center_x = (circle->x * 2 + circle->diameter) * 128;
    int32_t center_y = (circle->y * 2 + circle->diameter) * 128;
    draw_capsule(pixels, center_x, center_y, center_x, center_y,
                 circle->diameter * 128, dot_rgb(circle->color), circle->opacity);
}

static void trig_q14_interpolated(uint16_t phase_q8, int16_t *cosine, int16_t *sine)
{
    uint8_t phase = (uint8_t)(phase_q8 >> 8) & 63U;
    uint8_t fraction = (uint8_t)phase_q8;
    int16_t cosine0;
    int16_t sine0;
    int16_t cosine1;
    int16_t sine1;
    bloub_trig_q14(phase, &cosine0, &sine0);
    bloub_trig_q14((uint8_t)(phase + 1U), &cosine1, &sine1);
    *cosine = (int16_t)(cosine0 + ((int32_t)(cosine1 - cosine0) * fraction >> 8));
    *sine = (int16_t)(sine0 + ((int32_t)(sine1 - sine0) * fraction >> 8));
}

static void arc_point_q8(const bloub_arc_frame_t *arc, uint16_t phase_q8, int32_t *x, int32_t *y)
{
    int16_t cosine;
    int16_t sine;
    int16_t tilt_cosine;
    int16_t tilt_sine;
    trig_q14_interpolated(phase_q8, &cosine, &sine);
    bloub_trig_q14(arc->tilt, &tilt_cosine, &tilt_sine);
    int32_t px_q8 = (int32_t)cosine * arc->radius_x >> 6;
    int32_t py_q8 = (int32_t)sine * arc->radius_y >> 6;
    *x = arc->center_x_q8 + ((px_q8 * tilt_cosine - py_q8 * tilt_sine) >> 14);
    *y = arc->center_y_q8 + ((px_q8 * tilt_sine + py_q8 * tilt_cosine) >> 14);
}

static void draw_arcs(uint16_t *pixels, bool front)
{
    for (size_t arc_index = 0; arc_index < s_frame.arc_count; arc_index++) {
        const bloub_arc_frame_t *arc = &s_frame.arcs[arc_index];
        if (arc->opacity == 0 || arc->sweep == 0) continue;
        for (uint8_t i = 0; i < arc->sweep; i += 2) {
            uint8_t step = arc->sweep - i >= 2 ? 2 : 1;
            uint16_t phase0_q8 = (uint16_t)(arc->phase_q8 + (i << 8)) & 0x3FFFU;
            uint16_t phase1_q8 = (uint16_t)(arc->phase_q8 + ((i + step) << 8)) & 0x3FFFU;
            int16_t unused;
            int16_t depth0;
            int16_t depth1;
            trig_q14_interpolated(phase0_q8, &unused, &depth0);
            trig_q14_interpolated(phase1_q8, &unused, &depth1);
            bool front0 = depth0 >= 0;
            bool front1 = depth1 >= 0;
            if (arc->depth_sorted && front0 == front1 && front0 != front) continue;
            int32_t x0;
            int32_t y0;
            int32_t x1;
            int32_t y1;
            arc_point_q8(arc, phase0_q8, &x0, &y0);
            arc_point_q8(arc, phase1_q8, &x1, &y1);
            if (arc->depth_sorted && front0 != front1) {
                uint16_t split_q8 = bloub_zero_crossing_q8(depth0, depth1);
                int32_t split_x = x0 + (int32_t)(((int64_t)(x1 - x0) * split_q8) >> 8);
                int32_t split_y = y0 + (int32_t)(((int64_t)(y1 - y0) * split_q8) >> 8);
                if (front0 == front) {
                    x1 = split_x;
                    y1 = split_y;
                } else {
                    x0 = split_x;
                    y0 = split_y;
                }
            }
            uint32_t color = wheel_rgb((uint16_t)(arc->hue + (uint32_t)arc->hue_span * i / arc->sweep));
            draw_capsule(pixels, x0, y0, x1, y1, arc->width * 128,
                         color, arc->opacity);
        }
    }
}

static void render_stage(uint16_t *pixels)
{
    uint16_t background = swap16(rgb565(BACKGROUND_COLOR));
    for (size_t i = 0; i < STAGE_PIXELS; i++) pixels[i] = background;
    draw_arcs(pixels, false);
    for (size_t i = 0; i < s_frame.dot_count; i++) {
        if (s_frame.dots[i].behind) draw_circle(pixels, &s_frame.dots[i]);
    }
    draw_body(pixels);
    draw_arcs(pixels, true);
    for (size_t i = 0; i < s_frame.dot_count; i++) {
        if (!s_frame.dots[i].behind) draw_circle(pixels, &s_frame.dots[i]);
    }
    draw_eye(pixels, &s_frame.eyes[0]);
    draw_eye(pixels, &s_frame.eyes[1]);
}

static bool IRAM_ATTR transfer_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *event, void *user)
{
    (void)io;
    (void)event;
    BaseType_t awakened = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user, &awakened);
    return awakened == pdTRUE;
}

static bool wait_transfer(TickType_t timeout)
{
    return xSemaphoreTake(s_transfer_done, timeout) == pdTRUE;
}

static bool paint_background(void)
{
    uint16_t *buffer = s_buffers[0];
    const int rows = STAGE_PIXELS / BSP_LCD_W;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < BSP_LCD_W; x++) {
            buffer[y * BSP_LCD_W + x] = background_pixel(x, y);
        }
    }
    if (esp_lcd_panel_draw_bitmap(bsp_display_panel(), 0, 0, BSP_LCD_W, rows, buffer) != ESP_OK ||
        !wait_transfer(pdMS_TO_TICKS(1000))) return false;
    for (int y = rows; y < BSP_LCD_H; y++) {
        for (int x = 0; x < BSP_LCD_W; x++) {
            buffer[(y - rows) * BSP_LCD_W + x] = background_pixel(x, y);
        }
    }
    return esp_lcd_panel_draw_bitmap(bsp_display_panel(), 0, rows, BSP_LCD_W, BSP_LCD_H, buffer) == ESP_OK &&
           wait_transfer(pdMS_TO_TICKS(1000));
}

static void process_events(uint32_t now_ms)
{
    uint32_t events = __atomic_exchange_n(&s_pending_events, 0, __ATOMIC_RELAXED);
    if (events & EVENT_AUTOPLAY) s_autoplay = !s_autoplay;
    if (events & (EVENT_PREVIOUS | EVENT_NEXT)) {
        s_transition_from = s_frame;
        int next = (int)s_state + ((events & EVENT_PREVIOUS) ? -1 : 1);
        if (next < 0) next = BLOUB_STATE_COUNT - 1;
        else if (next >= BLOUB_STATE_COUNT) next = 0;
        s_state = (bloub_state_t)next;
        s_state_started_at = now_ms;
        s_transitioning = true;
    }
}

static void sample_frame(uint32_t now_ms)
{
    process_events(now_ms);
    uint32_t elapsed = now_ms - s_state_started_at;
    if (s_autoplay && elapsed >= bloub_state_duration_ms(s_state)) {
        s_transition_from = s_frame;
        s_state = (bloub_state_t)((s_state + 1) % BLOUB_STATE_COUNT);
        s_state_started_at = now_ms;
        s_transitioning = true;
        elapsed = 0;
    }
    bloub_frame_t target;
    bloub_sample(s_state, elapsed, &target);
    uint32_t morph = bloub_state_morph_ms(s_state);
    if (s_transitioning && elapsed < morph) {
        bloub_blend(&s_transition_from, &target, (uint16_t)(elapsed * 1000U / morph), &s_frame);
    } else {
        s_frame = target;
        s_transitioning = false;
    }
}

static void animation_task(void *argument)
{
    (void)argument;
    esp_lcd_panel_io_callbacks_t callbacks = { .on_color_trans_done = transfer_done };
    if (esp_lcd_panel_io_register_event_callbacks(bsp_display_io(), &callbacks, s_transfer_done) != ESP_OK ||
        !paint_background()) {
        ESP_LOGE(TAG, "display transfer setup failed");
        s_running = false;
    }

    uint8_t buffer_index = 0;
    uint8_t in_flight = 0;
    uint32_t frame_count = 0;
    uint64_t render_total_us = 0;
    uint32_t render_max_us = 0;
    int64_t perf_started_us = esp_timer_get_time();
    TickType_t next_wake = xTaskGetTickCount();

    while (s_running) {
        if (in_flight >= s_buffer_count) {
            if (!wait_transfer(pdMS_TO_TICKS(1000))) {
                ESP_LOGE(TAG, "display transfer timeout");
                break;
            }
            in_flight--;
        }
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        sample_frame(now_ms);
        int64_t render_started_us = esp_timer_get_time();
        render_stage(s_buffers[buffer_index]);
        uint32_t render_us = (uint32_t)(esp_timer_get_time() - render_started_us);
        render_total_us += render_us;
        if (render_us > render_max_us) render_max_us = render_us;

        if (esp_lcd_panel_draw_bitmap(bsp_display_panel(), STAGE_X, STAGE_Y,
                                      STAGE_X + BLOUB_STAGE_SIZE, STAGE_Y + BLOUB_STAGE_SIZE,
                                      s_buffers[buffer_index]) != ESP_OK) {
            ESP_LOGE(TAG, "frame transfer failed");
            break;
        }
        in_flight++;
        buffer_index = (uint8_t)((buffer_index + 1) % s_buffer_count);
        frame_count++;

        int64_t window_us = esp_timer_get_time() - perf_started_us;
        if (window_us >= 5000000) {
            uint32_t fps_tenths = (uint32_t)((uint64_t)frame_count * 10000000ULL / window_us);
            ESP_LOGI(TAG, "raster=%u.%u fps render_avg=%llu us render_max=%u us heap=%u state=%s",
                     fps_tenths / 10, fps_tenths % 10,
                     frame_count ? render_total_us / frame_count : 0, render_max_us,
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL), bloub_state_name(s_state));
            perf_started_us = esp_timer_get_time();
            frame_count = 0;
            render_total_us = 0;
            render_max_us = 0;
        }
        TickType_t before_delay = xTaskGetTickCount();
        xTaskDelayUntil(&next_wake, pdMS_TO_TICKS(FRAME_PERIOD_MS));
        if (xTaskGetTickCount() == before_delay) vTaskDelay(1);
    }

    while (in_flight > 0) {
        if (!wait_transfer(pdMS_TO_TICKS(1000))) break;
        in_flight--;
    }
    esp_lcd_panel_io_callbacks_t no_callbacks = {0};
    esp_lcd_panel_io_register_event_callbacks(bsp_display_io(), &no_callbacks, NULL);
    s_running = false;
    s_task = NULL;
    xSemaphoreGive(s_task_stopped);
    vTaskDelete(NULL);
}

void demo_bloub_enter(void)
{
    if (s_task) return;
    s_transfer_done = xSemaphoreCreateCounting(4, 0);
    s_task_stopped = xSemaphoreCreateBinary();
    if (!s_transfer_done || !s_task_stopped) {
        ESP_LOGE(TAG, "semaphore allocation failed");
        return;
    }
    for (size_t i = 0; i < 2; i++) {
        s_buffers[i] = heap_caps_malloc(STAGE_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_buffers[i]) break;
        s_buffer_count++;
    }
    if (s_buffer_count == 0) {
        ESP_LOGE(TAG, "framebuffer allocation failed");
        return;
    }

    s_autoplay = true;
    s_transitioning = false;
    s_state = BLOUB_STATE_IDLE;
    s_state_started_at = (uint32_t)(esp_timer_get_time() / 1000);
    s_pending_events = 0;
    bloub_sample(s_state, 0, &s_frame);
    s_running = true;
    if (xTaskCreate(animation_task, "bloub", 6144, NULL, 5, &s_task) != pdPASS) {
        s_running = false;
        s_task = NULL;
        ESP_LOGE(TAG, "animation task allocation failed");
        return;
    }
    ESP_LOGI(TAG, "antialiased raster scene ready bg=#efd7a5 eyes=#ffffff corner=%d buffers=%u",
             DISPLAY_CORNER_RADIUS, s_buffer_count);
}

void demo_bloub_exit(void)
{
    if (!s_task) return;
    s_running = false;
    xSemaphoreTake(s_task_stopped, pdMS_TO_TICKS(2500));
    for (size_t i = 0; i < s_buffer_count; i++) {
        heap_caps_free(s_buffers[i]);
        s_buffers[i] = NULL;
    }
    s_buffer_count = 0;
    vSemaphoreDelete(s_transfer_done);
    vSemaphoreDelete(s_task_stopped);
    s_transfer_done = NULL;
    s_task_stopped = NULL;
}

void demo_bloub_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_task || ev != BSP_BTN_CLICK) return;
    uint32_t event = btn == BSP_BTN_OK ? EVENT_AUTOPLAY
                     : btn == BSP_BTN_UP ? EVENT_PREVIOUS : EVENT_NEXT;
    __atomic_fetch_or(&s_pending_events, event, __ATOMIC_RELAXED);
}
