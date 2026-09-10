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
#define CONFIG_NICE_OLED_RIPPLE_DURATION_MS 320
#endif

#ifndef CONFIG_NICE_OLED_RIPPLE_FPS
#define CONFIG_NICE_OLED_RIPPLE_FPS 30
#endif

#ifndef CONFIG_NICE_OLED_RIPPLE_HUE_STEP
#define CONFIG_NICE_OLED_RIPPLE_HUE_STEP 15
#endif

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
    /* SW14 (ID 13) */ {  63,  37,  32,  49,  34,  57,  54,  80,  76,  78,  83,  66,  59,  57,  62,  40,  38,  43,  38,  43,  54,  45,  29,  19,  25,  48,  64,  19,   0,  19,  38,  40,  25,  19,  29,  44 },
    /* SW15 (ID 14) */ {  45,  29,  45,  64,  36,  46,  45,  63,  58,  58,  65,  49,  40,  39,  46,  38,  45,  29,  19,  25,  40,  38,  19,   0,  19,  61,  80,  29,  19,  25,  40,  51,  41,  38,  45,  62 },
    /* SW16 (ID 15) */ {  31,  37,  63,  79,  43,  36,  38,  45,  38,  41,  51,  38,  24,  20,  31,  40,  54,  19,   0,  19,  38,  45,  29,  19,  25,  74,  94,  43,  38,  43,  54,  67,  59,  57,  61,  82 },
    /* SW17 (ID 16) */ {  29,  53,  82,  94,  55,  32,  38,  27,  19,  27,  43,  38,  19,   0,  19,  49,  66,  24,  20,  31,  47,  59,  46,  39,  40,  88, 109,  59,  57,  62,  72,  86,  79,  76,  78, 101 },
    /* SW18 (ID 17) */ {  34,  68, 100, 112,  72,  43,  51,  19,   0,  19,  38,  43,  27,  19,  27,  65,  83,  41,  38,  45,  57,  73,  63,  58,  58, 105, 126,  78,  76,  80,  87, 103,  98,  95,  97, 120 },
    /* SW19 (ID 18) */ {  90,  65,  48,  21,  31,  66,  59,  95,  97, 102, 110,  94,  84,  78,  76,  41,  25,  57,  61,  70,  83,  73,  57,  45,  38,  21,  35,  19,  29,  45,  63,  57,  38,  19,   0,  40 },
    /* SW20 (ID 19) */ {  72,  54,  49,  36,  17,  48,  43,  76,  78,  83,  93,  78,  66,  59,  57,  25,  19,  38,  43,  54,  69,  63,  45,  29,  19,  32,  52,   0,  19,  38,  57,  58,  40,  25,  19,  52 },
    /* SW21 (ID 20) */ {  56,  48,  57,  54,  19,  32,  29,  58,  58,  65,  76,  63,  49,  40,  39,  19,  29,  19,  25,  40,  58,  57,  38,  19,   0,  49,  69,  19,  25,  40,  58,  65,  50,  41,  38,  68 },
    /* SW22 (ID 21) */ {  48,  54,  73,  71,  32,  17,  19,  38,  41,  51,  65,  56,  38,  24,  20,  25,  43,   0,  19,  38,  57,  63,  45,  29,  19,  65,  86,  38,  43,  54,  69,  79,  67,  59,  57,  86 },
    /* SW23 (ID 22) */ {  48,  68,  91,  89,  48,  16,  24,  19,  27,  43,  60,  57,  38,  19,   0,  40,  59,  20,  31,  47,  65,  75,  59,  46,  39,  81, 103,  57,  62,  72,  84,  97,  86,  79,  76, 106 },
    /* SW24 (ID 23) */ {  52,  80, 108, 108,  67,  33,  41,   0,  19,  38,  57,  60,  43,  27,  19,  58,  78,  38,  45,  57,  73,  86,  73,  63,  58, 100, 121,  76,  80,  87,  98, 112, 103,  98,  95, 124 },
    /* SW25 (ID 24) */ { 102,  65,  28,  51,  68,  99,  95, 124, 120, 119, 121, 102, 100, 101, 106,  78,  65,  86,  82,  81,  85,  69,  63,  62,  68,  58,  59,  52,  44,  44,  51,  34,  24,  27,  40,   0 },
    /* SW26 (ID 25) */ { 124, 101,  78,  15,  55,  89,  81, 121, 126, 134, 144, 129, 118, 109, 103,  63,  44,  86,  94, 105, 118, 108,  93,  80,  69,  21,   0,  52,  64,  79,  96,  87,  69,  51,  35,  59 },
    /* SW27 (ID 26) */ { 104,  85,  69,  10,  33,  68,  60, 100, 105, 114, 124, 110,  98,  88,  81,  42,  22,  65,  74,  86, 101,  93,  76,  61,  49,   0,  21,  32,  48,  66,  84,  78,  59,  40,  21,  58 },
    /* SW28 (ID 27) */ {  85,  72,  67,  31,  11,  46,  38,  78,  83,  92, 104,  91,  78,  66,  59,  19,   0,  43,  54,  69,  85,  81,  63,  45,  29,  22,  44,  19,  38,  57,  76,  76,  58,  40,  25,  65 },
    /* SW29 (ID 28) */ {  72,  68,  73,  50,  10,  26,  19,  58,  65,  76,  90,  79,  63,  49,  40,   0,  19,  25,  40,  58,  76,  76,  57,  38,  19,  42,  63,  25,  40,  58,  76,  81,  65,  51,  41,  78 },
    /* SW30 (ID 29) */ {  66,  73,  86,  69,  29,   8,   0,  41,  51,  65,  81,  74,  56,  38,  24,  19,  38,  19,  38,  57,  76,  81,  63,  45,  29,  60,  81,  43,  54,  69,  85,  93,  79,  67,  59,  95 },
};

/* Map ZMK matrix position (0..59) to switch index (0..29: SW1..SW30) */
static const int8_t pos_to_sw_id[60] = {
     5,  4,  3,  2,  1,  0, /* Left row 0: SW6..SW1 (pos 0..5) */
     0,  1,  2,  3,  4,  5, /* Right row 0: SW1..SW6 (pos 6..11) */
    11, 10,  9,  8,  7,  6, /* Left row 1: SW12..SW7 (pos 12..17) */
     6,  7,  8,  9, 10, 11, /* Right row 1: SW7..SW12 (pos 18..23) */
    17, 16, 15, 14, 13, 12, /* Left row 2: SW18..SW13 (pos 24..29) */
    12, 13, 14, 15, 16, 17, /* Right row 2: SW13..SW18 (pos 30..35) */
    23, 22, 21, 20, 19, 18, 24, /* Left row 3: SW24..SW19, SW25 (pos 36..42) */
    24, 18, 19, 20, 21, 22, 23, /* Right row 3: SW25, SW19..SW24 (pos 43..49) */
    29, 28, 27, 26, 25, /* Left row 4: SW30..SW26 (pos 50..54) */
    25, 26, 27, 28, 29  /* Right row 4: SW26..SW30 (pos 55..59) */
};

static const struct device *led_strip;
static struct led_rgb pixels[36];
static void ripple_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ripple_work, ripple_work_handler);

static volatile int8_t ripple_origin_sw = -1;
static volatile uint32_t ripple_start_time = 0;
static volatile uint16_t ripple_hue = 0;
static volatile bool ripple_active = false;

/* Fast integer HSB to RGB (0-360 hue, 0-100 sat, 0-100 brt) */
static struct led_rgb hsb_to_rgb(uint16_t h, uint8_t s, uint8_t v) {
    uint8_t r = 0, g = 0, b = 0;
    if (s == 0) {
        r = g = b = (v * 255) / 100;
    } else {
        uint8_t region = h / 60;
        uint16_t remainder = (h - (region * 60)) * 6;
        uint32_t val = (v * 255) / 100;
        uint32_t p = (val * (100 - s)) / 100;
        uint32_t q = (val * (100 - ((s * remainder) / 360))) / 100;
        uint32_t t = (val * (100 - ((s * (360 - remainder)) / 360))) / 100;
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

static void restore_underglow_state(void) {
    bool is_on = false;
    zmk_rgb_underglow_get_state(&is_on);
    if (is_on) {
        struct zmk_led_hsb base_hsb = zmk_rgb_underglow_calc_hue(0);
        struct led_rgb base_col = hsb_to_rgb(base_hsb.h, base_hsb.s, base_hsb.b);
        for (int i = 0; i < 36; i++) {
            pixels[i] = base_col;
        }
        led_strip_update_rgb(led_strip, pixels, 36);
    } else {
        for (int i = 0; i < 36; i++) {
            pixels[i] = (struct led_rgb){ .r = 0, .g = 0, .b = 0 };
        }
        led_strip_update_rgb(led_strip, pixels, 36);
    }
}

static void ripple_work_handler(struct k_work *work) {
    if (!ripple_active || !led_strip) {
        return;
    }

    uint32_t now = k_uptime_get_32();
    uint32_t elapsed = now - ripple_start_time;

    if (elapsed >= CONFIG_NICE_OLED_RIPPLE_DURATION_MS) {
        ripple_active = false;
        restore_underglow_state();
        return;
    }

    int8_t origin = ripple_origin_sw;
    if (origin < 0 || origin >= 30) {
        ripple_active = false;
        restore_underglow_state();
        return;
    }

    bool ug_on = false;
    zmk_rgb_underglow_get_state(&ug_on);
    struct led_rgb base_rgb = { .r = 0, .g = 0, .b = 0 };
    if (ug_on) {
        struct zmk_led_hsb ug_hsb = zmk_rgb_underglow_calc_hue(0);
        base_rgb = hsb_to_rgb(ug_hsb.h, ug_hsb.s, ug_hsb.b);
    }

    /* Wave front expands across board (~135 mm) over CONFIG_NICE_OLED_RIPPLE_DURATION_MS */
    uint32_t wave_r = (elapsed * 135) / CONFIG_NICE_OLED_RIPPLE_DURATION_MS;
    uint32_t wave_width = 24; /* 24 mm (~1.2 keys wide) */
    uint32_t fade = CONFIG_NICE_OLED_RIPPLE_DURATION_MS - elapsed;

    const uint8_t *dists = led_dist[origin];
    uint16_t hue = ripple_hue;

    for (int i = 0; i < 36; i++) {
        uint32_t d = dists[i];
        uint32_t diff = (d > wave_r) ? (d - wave_r) : (wave_r - d);

        if (diff < wave_width) {
            uint32_t pulse = wave_width - diff;
            uint32_t brt = (CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS * pulse * fade) /
                           (wave_width * CONFIG_NICE_OLED_RIPPLE_DURATION_MS);

            if (brt > CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS) {
                brt = CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS;
            }

            if (ug_on) {
                struct led_rgb wave_col = hsb_to_rgb(hue, 100, brt);
                uint16_t r = (uint16_t)base_rgb.r + wave_col.r;
                uint16_t g = (uint16_t)base_rgb.g + wave_col.g;
                uint16_t b = (uint16_t)base_rgb.b + wave_col.b;
                pixels[i].r = (r > 255) ? 255 : r;
                pixels[i].g = (g > 255) ? 255 : g;
                pixels[i].b = (b > 255) ? 255 : b;
            } else {
                pixels[i] = hsb_to_rgb(hue, 100, brt);
            }
        } else {
            pixels[i] = base_rgb;
        }
    }

    led_strip_update_rgb(led_strip, pixels, 36);

    /* Reschedule next frame in thread context (~30 FPS -> ~33ms) */
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
        if (ev->position < 60) {
            int8_t sw_id = pos_to_sw_id[ev->position];
            if (sw_id >= 0 && sw_id < 30) {
                ripple_origin_sw = sw_id;
                ripple_start_time = k_uptime_get_32();
                ripple_hue = (ripple_hue + CONFIG_NICE_OLED_RIPPLE_HUE_STEP) % 360;
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
