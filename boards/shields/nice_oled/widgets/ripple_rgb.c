/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>
#include "ripple_rgb.h"

#if DT_HAS_CHOSEN(zmk_underglow)

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define NUM_LEDS DT_PROP(STRIP_CHOSEN, chain_length)

#ifndef CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS
#define CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS 35
#endif

#ifndef CONFIG_NICE_OLED_RIPPLE_DURATION_MS
#define CONFIG_NICE_OLED_RIPPLE_DURATION_MS 800
#endif

#ifndef CONFIG_NICE_OLED_RIPPLE_FPS
#define CONFIG_NICE_OLED_RIPPLE_FPS 25
#endif

#ifndef CONFIG_NICE_OLED_RIPPLE_HUE_STEP
#define CONFIG_NICE_OLED_RIPPLE_HUE_STEP 15
#endif

#ifndef CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN 0
#endif

#ifndef CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX 100
#endif

#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

/* Native timer period confirmed in ZMK v0.3 app/src/rgb_underglow.c (K_MSEC(50)) */
#define ZMK_UNDERGLOW_TICK_PERIOD_MS 50

extern struct k_timer underglow_tick;

/*
 * Precomputed integer distances in mm from each switch (SW1..SW30) to each LED (0..35).
 * Ground truth extracted directly from PandaKB Sofle RGB KiCad PCB layout and netlist:
 * - LEDs 0..5:  Underglow drop lights (D33, D32, D31, D34, D35, D36)
 * - LEDs 6..34: Per-key backlight chain (SW30, SW24..SW6, SW5..SW23, SW29, SW28, SW22..SW4, SW3..SW21, SW27, SW26, SW20..SW2, SW1..SW19)
 * - LED 35:     Rotary encoder / layer area (SW25)
 */
static const uint8_t led_dist[30][36] = {
    /* SW1  (ID  0) */ {  78,  38,  10,  75,  74,  95,  93, 112, 103,  98,  95,  76,  79,  86,  97,  81,  76,  79,  67,  59,  57,  38,  41,  51,  65,  78,  87,  58,  40,  25,  19,   0,  19,  38,  57,  34 },
    /* SW2  (ID  1) */ {  60,  20,  22,  83,  71,  86,  85,  98,  87,  80,  76,  57,  62,  72,  84,  76,  76,  69,  54,  43,  38,  19,  25,  40,  58,  84,  96,  57,  38,  19,   0,  19,  29,  45,  63,  51 },
    /* SW3  (ID  2) */ {  42,   9,  41,  93,  74,  80,  81,  86,  73,  63,  58,  39,  46,  59,  75,  76,  81,  63,  45,  29,  19,   0,  19,  38,  57,  93, 108,  63,  45,  29,  19,  38,  45,  58,  73,  69 },
    /* SW4  (ID  3) */ {  24,  20,  58, 103,  76,  73,  76,  73,  57,  45,  39,  20,  31,  47,  65,  76,  85,  57,  38,  19,   0,  19,  25,  40,  58, 101, 118,  69,  54,  43,  38,  57,  61,  70,  83,  85 },
    /* SW5  (ID  4) */ {   9,  38,  77, 113,  81,  69,  74,  60,  43,  27,  19,   0,  19,  38,  57,  79,  91,  56,  38,  24,  20,  39,  40,  49,  63, 110, 129,  78,  66,  59,  57,  76,  78,  84,  94, 102 },
    /* SW6  (ID  5) */ {  20,  57,  96, 129,  94,  75,  81,  57,  38,  19,   0,  19,  27,  43,  60,  90, 104,  65,  51,  41,  39,  58,  58,  65,  76, 124, 144,  93,  83,  78,  76,  95,  97, 102, 110, 121 },
    /* SW7  (ID  6) */ {  77,  41,  10,  56,  57,  82,  79, 103,  98,  95,  97,  78,  76,  79,  86,  65,  58,  67,  59,  57,  61,  45,  38,  41,  50,  59,  69,  40,  25,  19,  29,  19,   0,  19,  38,  24 },
    /* SW8  (ID  7) */ {  58,  23,  20,  65,  53,  70,  69,  87,  80,  76,  78,  59,  57,  62,  72,  58,  57,  54,  43,  38,  43,  29,  19,  25,  40,  66,  79,  38,  19,   0,  19,  25,  19,  29,  45,  44 },
    /* SW9  (ID  8) */ {  39,  10,  38,  78,  55,  62,  63,  73,  63,  58,  58,  40,  39,  46,  59,  57,  63,  45,  29,  19,  25,  19,   0,  19,  38,  76,  93,  45,  29,  19,  25,  41,  38,  45,  57,  63 },
    /* SW10 (ID  9) */ {  21,  23,  58,  90,  59,  54,  57,  57,  45,  38,  41,  24,  20,  31,  47,  58,  69,  38,  19,   0,  19,  29,  19,  25,  40,  86, 105,  54,  43,  38,  43,  59,  57,  61,  70,  81 },
    /* SW11 (ID 10) */ {  10,  42,  77, 103,  67,  50,  56,  43,  27,  19,  27,  19,   0,  19,  38,  63,  78,  38,  24,  20,  31,  46,  39,  40,  49,  98, 118,  66,  59,  57,  62,  79,  76,  78,  84, 100 },
    /* SW12 (ID 11) */ {  21,  60,  96, 119,  82,  58,  65,  38,  19,   0,  19,  27,  19,  27,  43,  76,  92,  51,  41,  38,  45,  63,  58,  58,  65, 114, 134,  83,  78,  76,  80,  98,  95,  97, 102, 119 },
    /* SW13 (ID 12) */ {  82,  51,  29,  38,  42,  72,  67,  98,  95,  97, 102,  84,  78,  76,  79,  51,  40,  59,  57,  61,  70,  58,  45,  38,  41,  40,  51,  25,  19,  29,  45,  38,  19,   0,  19,  27 },
    /* SW14 (ID 13) */ {  63,  33,  29,  47,  36,  57,  54,  80,  76,  78,  84,  66,  59,  57,  62,  41,  38,  43,  38,  43,  54,  45,  29,  19,  25,  49,  63,  25,  19,  25,  41,  45,  29,  19,  25,  44 },
    /* SW15 (ID 14) */ {  44,  19,  42,  63,  38,  46,  47,  64,  58,  58,  66,  49,  40,  39,  46,  39,  45,  31,  20,  25,  40,  38,  19,   0,  19,  62,  79,  34,  25,  30,  46,  57,  45,  38,  41,  63 },
    /* SW16 (ID 15) */ {  27,  29,  63,  79,  44,  37,  40,  46,  38,  41,  50,  34,  20,  20,  31,  41,  54,  20,   0,  19,  38,  39,  20,  19,  31,  74,  93,  45,  38,  41,  50,  69,  59,  57,  61,  82 },
    /* SW17 (ID 16) */ {  20,  49,  82,  94,  54,  32,  39,  29,  19,  27,  38,  27,  19,   0,  19,  50,  66,  24,  19,  31,  47,  51,  38,  39,  46,  89, 108,  59,  57,  61,  70,  86,  79,  76,  79, 101 },
    /* SW18 (ID 17) */ {  32,  68, 102, 111,  72,  43,  50,  19,   0,  19,  27,  38,  27,  19,  27,  67,  83,  43,  38,  46,  59,  68,  57,  58,  62, 106, 126,  78,  76,  79,  86, 103,  98,  95,  97, 120 },
    /* SW19 (ID 18) */ {  93,  67,  49,  20,  35,  68,  61,  96,  97, 102, 110,  93,  83,  78,  76,  43,  25,  56,  62,  72,  84,  76,  59,  45,  38,  20,  34,  19,  29,  45,  63,  57,  38,  19,   0,  40 },
    /* SW20 (ID 19) */ {  73,  48,  44,  30,  23,  49,  43,  77,  78,  84,  93,  76,  66,  59,  57,  27,  19,  38,  43,  54,  69,  63,  43,  25,  19,  34,  49,  19,  25,  41,  58,  58,  38,  19,  19,  51 },
    /* SW21 (ID 20) */ {  54,  34,  51,  50,  24,  34,  32,  59,  59,  67,  78,  61,  48,  41,  38,  25,  34,  20,  25,  40,  58,  57,  34,  19,  25,  51,  69,  29,  30,  46,  63,  67,  49,  38,  38,  68 },
    /* SW22 (ID 21) */ {  39,  41,  73,  69,  32,  23,  24,  39,  39,  49,  63,  48,  31,  20,  19,  31,  46,  19,  19,  38,  57,  58,  39,  31,  40,  67,  86,  45,  45,  57,  72,  79,  67,  58,  57,  86 },
    /* SW23 (ID 22) */ {  37,  60,  92,  87,  46,  18,  23,  19,  19,  34,  51,  43,  27,  19,  27,  46,  62,  31,  34,  49,  66,  67,  51,  46,  54,  84, 104,  63,  62,  72,  84,  95,  87,  80,  77, 105 },
    /* SW24 (ID 23) */ {  49,  79, 112, 107,  67,  34,  39,   0,  19,  27,  43,  57,  43,  27,  19,  67,  82,  51,  50,  62,  76,  84,  68,  62,  69, 103, 123,  82,  80,  87,  98, 112, 103,  98,  95, 124 },
    /* SW25 (ID 24) */ {  87,  49,  29,  57,  67,  93,  92, 120, 118, 119, 121, 102,  98, 100, 105,  77,  68,  82,  81,  84,  92,  75,  63,  63,  68,  65,  69,  51,  44,  44,  51,  34,  24,  27,  40,   0 },
    /* SW26 (ID 25) */ {  95,  68,  57,  22,  17,  38,  31,  72,  76,  86,  97,  84,  75,  69,  67,  25,  39,  38,  47,  61,  77,  76,  59,  45,  40,  20,  37,  38,  44,  59,  76,  77,  58,  40,  27,  69 },
    /* SW27 (ID 26) */ { 108,  80,  69,  11,  18,  40,  31,  74,  80,  92, 103,  92,  84,  79,  76,  24,  45,  47,  58,  72,  87,  87,  71,  57,  51,   0,  20,  40,  50,  66,  84,  87,  69,  51,  34,  65 },
    /* SW28 (ID 27) */ {  78,  50,  49,  46,  18,  27,  20,  58,  63,  75,  88,  74,  62,  54,  50,  19,  45,  23,  34,  51,  70,  68,  49,  38,  39,  40,  57,  29,  38,  54,  72,  69,  50,  32,  25,  63 },
    /* SW29 (ID 28) */ {  63,  42,  56,  61,  23,  19,  11,  45,  50,  63,  78,  65,  51,  43,  38,  29,  54,  19,  31,  49,  69,  67,  49,  41,  47,  57,  75,  37,  45,  60,  78,  73,  56,  41,  38,  75 },
    /* SW30 (ID 29) */ {  60,  55,  80,  79,  38,  11,   0,  39,  43,  56,  72,  63,  49,  38,  32,  47,  68,  34,  40,  54,  72,  75,  59,  54,  61,  75,  94,  53,  58,  71,  88,  87,  72,  60,  58,  92 },
};

/* Matrix position (0..59) to switch ID (0..29) mapping for Sofle */
static const int8_t pos_to_sw_id[60] = {
     5,  4,  3,  2,  1,  0, /* Row 0: SW6, SW5, SW4, SW3, SW2, SW1 */
    11, 10,  9,  8,  7,  6, /* Row 1: SW12..SW7 */
    17, 16, 15, 14, 13, 12, /* Row 2: SW18..SW13 */
    23, 22, 21, 20, 19, 18, /* Row 3: SW24..SW19 */
    24, 29, 28, 27, 26, 25, /* Row 4: SW25, SW30, SW29, SW28, SW27, SW26 */
    /* Right half matrix indices (30..59) */
     0,  1,  2,  3,  4,  5,
     6,  7,  8,  9, 10, 11,
    12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23,
    25, 26, 27, 28, 29, 24,
};

static const struct device *led_strip;
static struct led_rgb pixels[36];
static void ripple_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ripple_work, ripple_work_handler);

static volatile int8_t ripple_origin_sw = -1;
static volatile uint32_t ripple_start_time = 0;
static volatile uint16_t ripple_hue = 0;
static volatile bool ripple_active = false;
static volatile int ripple_base_effect = -1;
static bool underglow_tick_paused = false;

static void pause_underglow_tick(void) {
    if (!underglow_tick_paused) {
        k_timer_stop(&underglow_tick);
        underglow_tick_paused = true;
    }
}

static void resume_underglow_tick(void) {
    if (underglow_tick_paused) {
        /* Preserve the exact native 50 ms period from ZMK v0.3 (K_MSEC(50)) */
        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(ZMK_UNDERGLOW_TICK_PERIOD_MS));
        underglow_tick_paused = false;
    }
}

static struct zmk_led_hsb hsb_scale_min_max(struct zmk_led_hsb hsb) {
    hsb.b = CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN +
            (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX - CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN) * hsb.b / BRT_MAX;
    return hsb;
}

static struct zmk_led_hsb hsb_scale_zero_max(struct zmk_led_hsb hsb) {
    hsb.b = hsb.b * CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX / BRT_MAX;
    return hsb;
}

/* Fast integer HSB to RGB (0-360 hue, 0-100 sat, 0-100 brt) */
static struct led_rgb hsb_to_rgb(uint16_t h, uint8_t s, uint8_t v) {
    uint8_t r = 0, g = 0, b = 0;
    if (s == 0) {
        r = g = b = (uint8_t)((v * 255) / 100);
    } else {
        uint8_t region = (h % 360) / 60;
        uint16_t remainder = ((h % 360) - (region * 60)) * 6;
        uint32_t val = (v * 255) / 100;
        uint32_t sat = (s * 255) / 100;
        uint32_t p = (val * (255 - sat)) / 255;
        uint32_t q = (val * (255 - ((sat * remainder) / 360))) / 255;
        uint32_t t = (val * (255 - ((sat * (360 - remainder)) / 360))) / 255;
        switch (region % 6) {
            case 0: r = val; g = t; b = p; break;
            case 1: r = q; g = val; b = p; break;
            case 2: r = p; g = val; b = t; break;
            case 3: r = p; g = q; b = val; break;
            case 4: r = t; g = p; b = val; break;
            case 5: default: r = val; g = p; b = q; break;
        }
    }
    return (struct led_rgb){ .r = r, .g = g, .b = b };
}

enum zmk_underglow_effect_id {
    UNDERGLOW_EFFECT_SOLID = 0,
    UNDERGLOW_EFFECT_BREATHE = 1,
    UNDERGLOW_EFFECT_SPECTRUM = 2,
    UNDERGLOW_EFFECT_SWIRL = 3,
};

/* Compatible base frame calculation using exact mathematical formulas of ZMK v0.3 */
static void compute_base_frame(struct led_rgb *base_pixels, int effect, struct zmk_led_hsb cur_hsb, uint32_t now) {
    switch (effect) {
    case UNDERGLOW_EFFECT_SOLID: {
        struct zmk_led_hsb scaled = hsb_scale_min_max(cur_hsb);
        struct led_rgb col = hsb_to_rgb(scaled.h, scaled.s, scaled.b);
        for (int i = 0; i < 36; i++) {
            base_pixels[i] = col;
        }
        break;
    }
    case UNDERGLOW_EFFECT_BREATHE: {
        uint32_t step = (now / 5) % 2400;
        int32_t diff = (int32_t)step - 1200;
        if (diff < 0) diff = -diff;
        struct zmk_led_hsb hsb = cur_hsb;
        hsb.b = diff / 12;
        struct zmk_led_hsb scaled = hsb_scale_zero_max(hsb);
        struct led_rgb col = hsb_to_rgb(scaled.h, scaled.s, scaled.b);
        for (int i = 0; i < 36; i++) {
            base_pixels[i] = col;
        }
        break;
    }
    case UNDERGLOW_EFFECT_SPECTRUM: {
        uint16_t hue = (cur_hsb.h + (now / 50)) % 360;
        struct zmk_led_hsb hsb = cur_hsb;
        hsb.h = hue;
        struct zmk_led_hsb scaled = hsb_scale_min_max(hsb);
        struct led_rgb col = hsb_to_rgb(scaled.h, scaled.s, scaled.b);
        for (int i = 0; i < 36; i++) {
            base_pixels[i] = col;
        }
        break;
    }
    case UNDERGLOW_EFFECT_SWIRL: {
        uint16_t time_offset = (now / 30) % 360;
        for (int i = 0; i < 36; i++) {
            uint16_t hue = (360 / 36 * i + time_offset) % 360;
            struct zmk_led_hsb hsb = cur_hsb;
            hsb.h = hue;
            struct zmk_led_hsb scaled = hsb_scale_min_max(hsb);
            base_pixels[i] = hsb_to_rgb(scaled.h, scaled.s, scaled.b);
        }
        break;
    }
    default: {
        struct zmk_led_hsb scaled = hsb_scale_min_max(cur_hsb);
        struct led_rgb col = hsb_to_rgb(scaled.h, scaled.s, scaled.b);
        for (int i = 0; i < 36; i++) {
            base_pixels[i] = col;
        }
        break;
    }
    }
}

static void ripple_work_handler(struct k_work *work) {
    if (!ripple_active || !led_strip) {
        resume_underglow_tick();
        return;
    }

    /* 1. Confirm RGB is still ON */
    bool is_on = false;
    if (zmk_rgb_underglow_get_state(&is_on) != 0 || !is_on) {
        /* User turned off RGB during ripple */
        ripple_active = false;
        underglow_tick_paused = false;
        for (int i = 0; i < 36; i++) {
            pixels[i] = (struct led_rgb){ .r = 0, .g = 0, .b = 0 };
        }
        led_strip_update_rgb(led_strip, pixels, 36);
        return;
    }

    /* 2. Confirm active effect hasn't changed */
    int cur_effect = zmk_rgb_underglow_calc_effect(0);
    if (cur_effect != ripple_base_effect) {
        /* User changed effect via keymap (RGB_EFF) -> cancel ripple, yield to new effect */
        ripple_active = false;
        resume_underglow_tick();
        return;
    }

    /* 3. Check elapsed animation time */
    uint32_t now = k_uptime_get_32();
    uint32_t elapsed = now - ripple_start_time;
    if (elapsed >= CONFIG_NICE_OLED_RIPPLE_DURATION_MS) {
        ripple_active = false;
        resume_underglow_tick();
        return;
    }

    int8_t origin = ripple_origin_sw;
    if (origin < 0 || origin >= 30) {
        ripple_active = false;
        resume_underglow_tick();
        return;
    }

    /* 4. Pause native timer while ripple renders to eliminate bus contention */
    pause_underglow_tick();

    /* 5. Dynamically read current HSB (reflects live brightness / hue adjustments) */
    struct zmk_led_hsb cur_hsb = zmk_rgb_underglow_calc_hue(0);

    /* 6. Compute base effect frame */
    struct led_rgb base_frame[36];
    compute_base_frame(base_frame, cur_effect, cur_hsb, now);

    /* 7. Compute ripple wave (wave expands across ~135 mm, wave_width 28 mm) */
    uint32_t duration = CONFIG_NICE_OLED_RIPPLE_DURATION_MS;
    uint32_t wave_r = (elapsed * 135) / duration;
    uint32_t wave_width = 28; /* 28 mm (~1.5 keys wide) */
    uint32_t fade = duration - elapsed;

    const uint8_t *dists = led_dist[origin];
    uint16_t hue = ripple_hue;

    for (int i = 0; i < 36; i++) {
        uint32_t d = dists[i];
        uint32_t diff = (d > wave_r) ? (d - wave_r) : (wave_r - d);

        if (diff < wave_width) {
            uint32_t pulse = wave_width - diff;
            uint32_t brt = (CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS * pulse * fade) /
                           (wave_width * duration);
            if (brt > CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS) {
                brt = CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS;
            }

            struct led_rgb wave_col = hsb_to_rgb(hue, 100, brt);
            uint16_t r = (uint16_t)base_frame[i].r + wave_col.r;
            uint16_t g = (uint16_t)base_frame[i].g + wave_col.g;
            uint16_t b = (uint16_t)base_frame[i].b + wave_col.b;
            pixels[i].r = (r > 255) ? 255 : r;
            pixels[i].g = (g > 255) ? 255 : g;
            pixels[i].b = (b > 255) ? 255 : b;
        } else {
            pixels[i] = base_frame[i];
        }
    }

    led_strip_update_rgb(led_strip, pixels, 36);

    /* 8. Reschedule next frame (~25 FPS -> 40 ms) */
    uint32_t frame_interval_ms = 1000 / CONFIG_NICE_OLED_RIPPLE_FPS;
    if (frame_interval_ms < 15) {
        frame_interval_ms = 15;
    }
    k_work_schedule(&ripple_work, K_MSEC(frame_interval_ms));
}

/*
 * Event listener: Only stores keypress position and schedules thread work.
 * ZERO blocking calls, ZERO mutex operations, ZERO SPI access here.
 */
static int ripple_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->state) {
        /* Check if RGB is enabled. If OFF, do NOT start ripple! */
        bool is_on = false;
        if (zmk_rgb_underglow_get_state(&is_on) != 0 || !is_on) {
            return 0;
        }

        if (ev->position < 60) {
            int8_t sw_id = pos_to_sw_id[ev->position];
            if (sw_id >= 0 && sw_id < 30) {
                ripple_origin_sw = sw_id;
                ripple_start_time = k_uptime_get_32();
                ripple_hue = (ripple_hue + CONFIG_NICE_OLED_RIPPLE_HUE_STEP) % 360;
                ripple_base_effect = zmk_rgb_underglow_calc_effect(0);
                ripple_active = true;
                k_work_reschedule(&ripple_work, K_NO_WAIT);
            }
        }
    }
    return 0;
}

ZMK_LISTENER(ripple_rgb, ripple_listener);
ZMK_SUBSCRIPTION(ripple_rgb, zmk_position_state_changed);

static int ripple_rgb_init(const struct device *dev) {
    led_strip = DEVICE_DT_GET(STRIP_CHOSEN);
    if (!device_is_ready(led_strip)) {
        LOG_WRN("LED strip device not ready for ripple");
        return -ENODEV;
    }
    return 0;
}

SYS_INIT(ripple_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* DT_HAS_CHOSEN(zmk_underglow) */

void ripple_rgb_init_widget(void) {
    /* Self-initialized via SYS_INIT and ZMK_LISTENER */
}
