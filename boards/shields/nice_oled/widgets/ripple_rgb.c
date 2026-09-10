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
#include <string.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>
#include "ripple_rgb.h"

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#else
#include <zmk/split/transport/peripheral.h>
#include <zmk/split/transport/types.h>
#endif
#endif

#if DT_HAS_CHOSEN(zmk_underglow)

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define NUM_LEDS DT_PROP(STRIP_CHOSEN, chain_length)

#ifndef CONFIG_NICE_OLED_RIPPLE_HUE
#define CONFIG_NICE_OLED_RIPPLE_HUE 240
#endif

#ifndef CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS
#define CONFIG_NICE_OLED_RIPPLE_PEAK_BRIGHTNESS 35
#endif

#ifndef CONFIG_NICE_OLED_RIPPLE_DURATION_MS
#define CONFIG_NICE_OLED_RIPPLE_DURATION_MS 800
#endif

#ifndef CONFIG_NICE_OLED_RIPPLE_FPS
#define CONFIG_NICE_OLED_RIPPLE_FPS 25
#endif

#define ZMK_UNDERGLOW_TICK_PERIOD_MS 50

#if IS_ENABLED(CONFIG_SHIELD_SOFLE_R) || IS_ENABLED(CONFIG_SHIELD_SOFLE_DONGLE_RIGHT)
#define THIS_HALF_IS_RIGHT 1
#else
#define THIS_HALF_IS_RIGHT 0
#endif

/*
 * Precomputed physical distances in mm from each global switch (0..59) to each LED (0..35) of this half.
 * Derived directly from Sofle RGB KiCad PCB Edge.Cuts (inner edge 82.10 mm) and an 80 mm central desk gap.
 * - Switches  0..29: Left half SW1..SW30
 * - Switches 30..59: Right half SW1..SW30
 * Symmetrical property:
 *   On Left half:  dist = cross_dist[global_sw_id][led_idx]
 *   On Right half: dist = cross_dist[(global_sw_id < 30) ? (global_sw_id + 30) : (global_sw_id - 30)][led_idx]
 */
static const uint16_t cross_dist[60][36] = {
    /* ID  0 (SW1_L  ) */ {  78,  38,  10,  75,  74,  95,  93, 112, 103,  98,  95,  76,  79,  86,  97,  81,  76,  79,  67,  59,  57,  38,  41,  51,  65,  78,  87,  58,  40,  25,  19,   0,  19,  38,  57,  34 },
    /* ID  1 (SW2_L  ) */ {  60,  20,  22,  83,  71,  86,  85,  98,  87,  80,  76,  57,  62,  72,  84,  76,  76,  69,  54,  43,  38,  19,  25,  40,  58,  84,  96,  57,  38,  19,   0,  19,  29,  45,  63,  51 },
    /* ID  2 (SW3_L  ) */ {  42,   9,  41,  93,  74,  80,  81,  86,  73,  63,  58,  39,  46,  59,  75,  76,  81,  63,  45,  29,  19,   0,  19,  38,  57,  93, 108,  63,  45,  29,  19,  38,  45,  58,  73,  69 },
    /* ID  3 (SW4_L  ) */ {  24,  20,  58, 103,  76,  73,  76,  73,  57,  45,  39,  20,  31,  47,  65,  76,  85,  57,  38,  19,   0,  19,  25,  40,  58, 101, 118,  69,  54,  43,  38,  57,  61,  70,  83,  85 },
    /* ID  4 (SW5_L  ) */ {   9,  38,  77, 113,  81,  69,  74,  60,  43,  27,  19,   0,  19,  38,  57,  79,  91,  56,  38,  24,  20,  39,  40,  49,  63, 110, 129,  78,  66,  59,  57,  76,  78,  84,  94, 102 },
    /* ID  5 (SW6_L  ) */ {  20,  57,  96, 129,  94,  75,  81,  57,  38,  19,   0,  19,  27,  43,  60,  90, 104,  65,  51,  41,  39,  58,  58,  65,  76, 124, 144,  93,  83,  78,  76,  95,  97, 102, 110, 121 },
    /* ID  6 (SW7_L  ) */ {  77,  41,  10,  56,  57,  82,  79, 103,  98,  95,  97,  78,  76,  79,  86,  65,  58,  67,  59,  57,  61,  45,  38,  41,  50,  59,  69,  40,  25,  19,  29,  19,   0,  19,  38,  24 },
    /* ID  7 (SW8_L  ) */ {  58,  23,  20,  65,  53,  70,  69,  87,  80,  76,  78,  59,  57,  62,  72,  58,  57,  54,  43,  38,  43,  29,  19,  25,  40,  66,  79,  38,  19,   0,  19,  25,  19,  29,  45,  44 },
    /* ID  8 (SW9_L  ) */ {  39,  10,  38,  78,  55,  62,  63,  73,  63,  58,  58,  40,  39,  46,  59,  57,  63,  45,  29,  19,  25,  19,   0,  19,  38,  76,  93,  45,  29,  19,  25,  41,  38,  45,  57,  63 },
    /* ID  9 (SW10_L ) */ {  21,  23,  58,  90,  59,  54,  57,  57,  45,  38,  41,  24,  20,  31,  47,  58,  69,  38,  19,   0,  19,  29,  19,  25,  40,  86, 105,  54,  43,  38,  43,  59,  57,  61,  70,  81 },
    /* ID 10 (SW11_L ) */ {  10,  42,  77, 103,  67,  50,  56,  43,  27,  19,  27,  19,   0,  19,  38,  63,  78,  38,  24,  20,  31,  46,  39,  40,  49,  98, 118,  66,  59,  57,  62,  79,  76,  78,  84, 100 },
    /* ID 11 (SW12_L ) */ {  21,  60,  96, 119,  82,  58,  65,  38,  19,   0,  19,  27,  19,  27,  43,  76,  92,  51,  41,  38,  45,  63,  58,  58,  65, 114, 134,  83,  78,  76,  80,  98,  95,  97, 102, 119 },
    /* ID 12 (SW13_L ) */ {  82,  51,  29,  38,  42,  72,  67,  98,  95,  97, 102,  84,  78,  76,  79,  51,  40,  59,  57,  61,  70,  58,  45,  38,  41,  40,  51,  25,  19,  29,  45,  38,  19,   0,  19,  27 },
    /* ID 13 (SW14_L ) */ {  63,  37,  32,  49,  34,  57,  54,  80,  76,  78,  83,  66,  59,  57,  62,  40,  38,  43,  38,  43,  54,  45,  29,  19,  25,  48,  64,  19,   0,  19,  38,  40,  25,  19,  29,  44 },
    /* ID 14 (SW15_L ) */ {  45,  29,  45,  64,  36,  46,  45,  63,  58,  58,  65,  49,  40,  39,  46,  38,  45,  29,  19,  25,  40,  38,  19,   0,  19,  61,  80,  29,  19,  25,  40,  51,  41,  38,  45,  62 },
    /* ID 15 (SW16_L ) */ {  31,  37,  63,  79,  43,  36,  38,  45,  38,  41,  51,  38,  24,  20,  31,  40,  54,  19,   0,  19,  38,  45,  29,  19,  25,  74,  94,  43,  38,  43,  54,  67,  59,  57,  61,  82 },
    /* ID 16 (SW17_L ) */ {  29,  53,  82,  94,  55,  32,  38,  27,  19,  27,  43,  38,  19,   0,  19,  49,  66,  24,  20,  31,  47,  59,  46,  39,  40,  88, 109,  59,  57,  62,  72,  86,  79,  76,  78, 101 },
    /* ID 17 (SW18_L ) */ {  34,  68, 100, 112,  72,  43,  51,  19,   0,  19,  38,  43,  27,  19,  27,  65,  83,  41,  38,  45,  57,  73,  63,  58,  58, 105, 126,  78,  76,  80,  87, 103,  98,  95,  97, 120 },
    /* ID 18 (SW19_L ) */ {  90,  65,  48,  21,  31,  66,  59,  95,  97, 102, 110,  94,  84,  78,  76,  41,  25,  57,  61,  70,  83,  73,  57,  45,  38,  21,  35,  19,  29,  45,  63,  57,  38,  19,   0,  40 },
    /* ID 19 (SW20_L ) */ {  72,  54,  49,  36,  17,  48,  43,  76,  78,  83,  93,  78,  66,  59,  57,  25,  19,  38,  43,  54,  69,  63,  45,  29,  19,  32,  52,   0,  19,  38,  57,  58,  40,  25,  19,  52 },
    /* ID 20 (SW21_L ) */ {  56,  48,  57,  54,  19,  32,  29,  58,  58,  65,  76,  63,  49,  40,  39,  19,  29,  19,  25,  40,  58,  57,  38,  19,   0,  49,  69,  19,  25,  40,  58,  65,  50,  41,  38,  68 },
    /* ID 21 (SW22_L ) */ {  48,  54,  73,  71,  32,  17,  19,  38,  41,  51,  65,  56,  38,  24,  20,  25,  43,   0,  19,  38,  57,  63,  45,  29,  19,  65,  86,  38,  43,  54,  69,  79,  67,  59,  57,  86 },
    /* ID 22 (SW23_L ) */ {  48,  68,  91,  89,  48,  16,  24,  19,  27,  43,  60,  57,  38,  19,   0,  40,  59,  20,  31,  47,  65,  75,  59,  46,  39,  81, 103,  57,  62,  72,  84,  97,  86,  79,  76, 106 },
    /* ID 23 (SW24_L ) */ {  52,  80, 108, 108,  67,  33,  41,   0,  19,  38,  57,  60,  43,  27,  19,  58,  78,  38,  45,  57,  73,  86,  73,  63,  58, 100, 121,  76,  80,  87,  98, 112, 103,  98,  95, 124 },
    /* ID 24 (SW25_L ) */ { 102,  65,  28,  51,  68,  99,  95, 124, 120, 119, 121, 102, 100, 101, 106,  78,  65,  86,  82,  81,  85,  69,  63,  62,  68,  58,  59,  52,  44,  44,  51,  34,  24,  27,  40,   0 },
    /* ID 25 (SW26_L ) */ { 124, 101,  78,  15,  55,  89,  81, 121, 126, 134, 144, 129, 118, 109, 103,  63,  44,  86,  94, 105, 118, 108,  93,  80,  69,  21,   0,  52,  64,  79,  96,  87,  69,  51,  35,  59 },
    /* ID 26 (SW27_L ) */ { 104,  85,  69,  10,  33,  68,  60, 100, 105, 114, 124, 110,  98,  88,  81,  42,  22,  65,  74,  86, 101,  93,  76,  61,  49,   0,  21,  32,  48,  66,  84,  78,  59,  40,  21,  58 },
    /* ID 27 (SW28_L ) */ {  85,  72,  67,  31,  11,  46,  38,  78,  83,  92, 104,  91,  78,  66,  59,  19,   0,  43,  54,  69,  85,  81,  63,  45,  29,  22,  44,  19,  38,  57,  76,  76,  58,  40,  25,  65 },
    /* ID 28 (SW29_L ) */ {  72,  68,  73,  50,  10,  26,  19,  58,  65,  76,  90,  79,  63,  49,  40,   0,  19,  25,  40,  58,  76,  76,  57,  38,  19,  42,  63,  25,  40,  58,  76,  81,  65,  51,  41,  78 },
    /* ID 29 (SW30_L ) */ {  66,  73,  86,  69,  29,   8,   0,  41,  51,  65,  81,  74,  56,  38,  24,  19,  38,  19,  38,  57,  76,  81,  63,  45,  29,  60,  81,  43,  54,  69,  85,  93,  79,  67,  59,  95 },
    /* ID 30 (SW1_R  ) */ { 234, 195, 157, 163, 198, 232, 226, 259, 255, 253, 252, 233, 234, 236, 240, 208, 191, 221, 217, 215, 214, 195, 195, 198, 202, 173, 157, 184, 179, 177, 176, 157, 158, 161, 167, 135 },
    /* ID 31 (SW2_R  ) */ { 253, 214, 176, 181, 216, 251, 245, 278, 274, 272, 271, 252, 253, 256, 260, 226, 209, 240, 236, 234, 233, 214, 215, 217, 221, 191, 175, 203, 198, 196, 195, 176, 177, 180, 186, 154 },
    /* ID 32 (SW3_R  ) */ { 272, 233, 195, 200, 235, 270, 264, 297, 294, 291, 290, 271, 272, 275, 279, 245, 228, 259, 255, 253, 252, 233, 234, 236, 240, 209, 193, 222, 218, 215, 214, 195, 196, 200, 205, 174 },
    /* ID 33 (SW4_R  ) */ { 291, 252, 214, 216, 253, 288, 282, 315, 312, 310, 309, 290, 291, 293, 297, 263, 245, 277, 274, 272, 271, 252, 253, 255, 258, 226, 209, 240, 236, 234, 233, 214, 215, 218, 222, 192 },
    /* ID 34 (SW5_R  ) */ { 310, 271, 233, 233, 270, 305, 299, 333, 330, 329, 328, 309, 310, 312, 314, 280, 262, 295, 292, 290, 290, 271, 271, 273, 276, 242, 224, 257, 254, 252, 252, 233, 234, 236, 239, 210 },
    /* ID 35 (SW6_R  ) */ { 329, 290, 252, 251, 288, 324, 317, 352, 349, 348, 347, 328, 329, 331, 333, 298, 280, 314, 311, 310, 309, 290, 290, 292, 294, 261, 242, 276, 273, 271, 271, 252, 253, 255, 258, 229 },
    /* ID 36 (SW7_R  ) */ { 234, 195, 157, 155, 192, 227, 221, 255, 253, 252, 253, 234, 233, 234, 236, 202, 184, 217, 215, 214, 215, 196, 195, 195, 198, 165, 148, 179, 177, 176, 177, 158, 157, 158, 161, 133 },
    /* ID 37 (SW8_R  ) */ { 253, 214, 176, 174, 211, 246, 240, 274, 272, 271, 271, 252, 252, 253, 256, 221, 203, 236, 234, 233, 234, 215, 214, 215, 217, 184, 166, 198, 196, 195, 196, 177, 176, 177, 180, 152 },
    /* ID 38 (SW9_R  ) */ { 272, 233, 195, 193, 230, 265, 259, 294, 291, 290, 290, 271, 271, 272, 275, 240, 222, 255, 253, 252, 253, 234, 233, 234, 236, 202, 185, 218, 215, 214, 215, 195, 195, 196, 200, 171 },
    /* ID 39 (SW10_R ) */ { 291, 252, 214, 210, 248, 284, 277, 312, 310, 309, 310, 290, 290, 291, 293, 258, 240, 274, 272, 271, 272, 253, 252, 253, 255, 220, 202, 236, 234, 233, 234, 215, 214, 215, 218, 190 },
    /* ID 40 (SW11_R ) */ { 310, 272, 233, 228, 266, 302, 295, 330, 329, 328, 329, 310, 309, 310, 312, 276, 257, 292, 290, 290, 291, 272, 271, 271, 273, 237, 218, 254, 252, 252, 253, 234, 233, 234, 236, 209 },
    /* ID 41 (SW12_R ) */ { 329, 291, 252, 246, 284, 320, 314, 349, 348, 347, 348, 329, 328, 329, 330, 294, 276, 311, 309, 309, 310, 291, 290, 290, 292, 256, 237, 273, 271, 271, 272, 253, 252, 253, 255, 228 },
    /* ID 42 (SW13_R ) */ { 235, 198, 159, 150, 188, 224, 217, 253, 252, 253, 255, 236, 234, 233, 234, 198, 179, 215, 214, 215, 218, 200, 196, 195, 195, 159, 141, 177, 176, 177, 180, 161, 158, 157, 158, 134 },
    /* ID 43 (SW14_R ) */ { 254, 216, 178, 169, 207, 243, 236, 272, 271, 271, 273, 254, 252, 252, 253, 217, 198, 234, 233, 234, 236, 218, 215, 214, 215, 178, 159, 196, 195, 196, 198, 179, 177, 176, 177, 152 },
    /* ID 44 (SW15_R ) */ { 273, 235, 196, 188, 226, 262, 255, 291, 290, 290, 292, 273, 271, 271, 272, 236, 218, 253, 252, 253, 255, 236, 234, 233, 234, 197, 178, 215, 214, 215, 217, 198, 195, 195, 196, 171 },
    /* ID 45 (SW16_R ) */ { 292, 254, 215, 206, 245, 281, 274, 310, 309, 309, 311, 292, 290, 290, 291, 255, 236, 272, 271, 272, 274, 255, 253, 252, 253, 215, 196, 234, 233, 234, 236, 217, 215, 214, 215, 190 },
    /* ID 46 (SW17_R ) */ { 311, 273, 235, 224, 263, 299, 292, 329, 328, 329, 331, 312, 310, 309, 310, 273, 254, 290, 290, 291, 293, 275, 272, 271, 271, 233, 214, 252, 252, 253, 256, 236, 234, 233, 234, 210 },
    /* ID 47 (SW18_R ) */ { 330, 292, 254, 243, 282, 318, 311, 348, 347, 348, 349, 330, 329, 328, 329, 292, 273, 309, 309, 310, 312, 294, 291, 290, 290, 252, 232, 271, 271, 272, 274, 255, 253, 252, 253, 229 },
    /* ID 48 (SW19_R ) */ { 238, 202, 164, 146, 186, 222, 215, 252, 253, 255, 258, 239, 236, 234, 233, 195, 177, 214, 215, 218, 222, 205, 200, 196, 195, 155, 136, 176, 177, 180, 186, 167, 161, 158, 157, 137 },
    /* ID 49 (SW20_R ) */ { 257, 220, 182, 165, 205, 241, 234, 271, 271, 273, 276, 257, 254, 252, 252, 215, 196, 233, 234, 236, 240, 222, 218, 215, 214, 175, 155, 195, 196, 198, 203, 184, 179, 177, 176, 155 },
    /* ID 50 (SW21_R ) */ { 275, 238, 200, 185, 224, 260, 253, 290, 290, 292, 294, 276, 273, 271, 271, 234, 215, 252, 253, 255, 258, 240, 236, 234, 233, 194, 174, 214, 215, 217, 221, 202, 198, 195, 195, 173 },
    /* ID 51 (SW22_R ) */ { 294, 257, 219, 203, 243, 279, 272, 309, 309, 311, 314, 295, 292, 290, 290, 253, 234, 271, 272, 274, 277, 259, 255, 253, 252, 212, 192, 233, 234, 236, 240, 221, 217, 215, 214, 192 },
    /* ID 52 (SW23_R ) */ { 314, 277, 238, 222, 262, 298, 290, 328, 329, 330, 333, 314, 312, 310, 309, 271, 252, 290, 291, 293, 297, 279, 275, 272, 271, 231, 211, 252, 253, 256, 260, 240, 236, 234, 233, 212 },
    /* ID 53 (SW24_R ) */ { 333, 295, 257, 241, 281, 317, 309, 347, 348, 349, 352, 333, 330, 329, 328, 290, 271, 309, 310, 312, 315, 297, 294, 291, 290, 250, 230, 271, 272, 274, 278, 259, 255, 253, 252, 231 },
    /* ID 54 (SW25_R ) */ { 210, 172, 134, 131, 167, 203, 196, 231, 229, 228, 229, 210, 209, 210, 212, 177, 160, 192, 190, 190, 192, 174, 171, 171, 173, 141, 124, 155, 152, 152, 154, 135, 133, 134, 137, 109 },
    /* ID 55 (SW26_R ) */ { 222, 189, 152, 122, 162, 198, 190, 230, 232, 237, 242, 224, 218, 214, 211, 172, 152, 192, 196, 202, 209, 193, 185, 178, 174, 130, 109, 155, 159, 166, 175, 157, 148, 141, 136, 124 },
    /* ID 56 (SW27_R ) */ { 240, 206, 169, 142, 183, 219, 211, 250, 252, 256, 261, 242, 237, 233, 231, 192, 173, 212, 215, 220, 226, 209, 202, 197, 194, 151, 130, 175, 178, 184, 191, 173, 165, 159, 155, 141 },
    /* ID 57 (SW28_R ) */ { 260, 225, 187, 164, 204, 240, 233, 271, 273, 276, 280, 262, 257, 254, 252, 214, 195, 234, 236, 240, 245, 228, 222, 218, 215, 173, 152, 196, 198, 203, 209, 191, 184, 179, 177, 160 },
    /* ID 58 (SW29_R ) */ { 278, 242, 204, 183, 224, 259, 252, 290, 292, 294, 298, 280, 276, 273, 271, 233, 214, 253, 255, 258, 263, 245, 240, 236, 234, 192, 172, 215, 217, 221, 226, 208, 202, 198, 195, 177 },
    /* ID 59 (SW30_R ) */ { 298, 261, 223, 202, 243, 278, 271, 309, 311, 314, 317, 299, 295, 292, 290, 252, 233, 272, 274, 277, 282, 264, 259, 255, 253, 211, 190, 234, 236, 240, 245, 226, 221, 217, 215, 196 },
};

/* Map ZMK matrix position (0..59) directly to unique global switch ID (0..59) */
static const int8_t pos_to_global_sw_id[60] = {
     5,  4,  3,  2,  1,  0, 30, 31, 32, 33, 34, 35,
    11, 10,  9,  8,  7,  6, 36, 37, 38, 39, 40, 41,
    17, 16, 15, 14, 13, 12, 42, 43, 44, 45, 46, 47,
    23, 22, 21, 20, 19, 18, 24, 54, 48, 49, 50, 51,
    52, 53, 29, 28, 27, 26, 25, 55, 56, 57, 58, 59,
};

static const struct device *s_led_strip;
static struct led_rgb s_pixels[36];
static void ripple_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_ripple_work, ripple_work_handler);

static volatile int s_extended_effect = 0;
static volatile uint16_t s_ripple_hue = CONFIG_NICE_OLED_RIPPLE_HUE;
static volatile int8_t s_ripple_origin_sw = -1;
static volatile uint32_t s_ripple_start_time = 0;
static volatile bool s_ripple_active = false;
static volatile uint16_t s_local_event_counter = 0;
static volatile uint32_t s_last_handled_event_key = 0;
static bool s_underglow_tick_paused = false;

extern struct k_timer underglow_tick;

extern int __real_zmk_rgb_underglow_calc_effect(int direction);
extern int __real_zmk_rgb_underglow_select_effect(int effect);
extern int __real_zmk_rgb_underglow_cycle_effect(int direction);
extern int __real_zmk_rgb_underglow_on(void);
extern int __real_zmk_rgb_underglow_off(void);

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
extern int __real_zmk_split_transport_peripheral_command_handler(
    const struct zmk_split_transport_peripheral *transport,
    struct zmk_split_transport_central_command cmd);
#endif

static void pause_underglow_tick(void) {
    if (!s_underglow_tick_paused) {
        bool is_on = false;
        if (zmk_rgb_underglow_get_state(&is_on) == 0 && is_on) {
            k_timer_stop(&underglow_tick);
            s_underglow_tick_paused = true;
        }
    }
}

static void resume_underglow_tick(void) {
    if (s_underglow_tick_paused) {
        bool is_on = false;
        if (zmk_rgb_underglow_get_state(&is_on) == 0 && is_on) {
            k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(ZMK_UNDERGLOW_TICK_PERIOD_MS));
        }
        s_underglow_tick_paused = false;
    }
}

static void ripple_clear_leds(void) {
    if (!s_led_strip) {
        return;
    }
    memset(s_pixels, 0, sizeof(s_pixels));
    led_strip_update_rgb(s_led_strip, s_pixels, 36);
}

static void ripple_cancel_active(void) {
    s_ripple_active = false;
    s_ripple_origin_sw = -1;
    k_work_cancel_delayable(&s_ripple_work);
    ripple_clear_leds();
}

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
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = val; break;
            case 5: default: r = val; g = p; b = q; break;
        }
    }
    return (struct led_rgb){ .r = r, .g = g, .b = b };
}

static void ripple_trigger_wave(uint8_t global_sw_id, uint16_t hue, uint16_t event_id) {
    if (global_sw_id >= 60) {
        return;
    }
    uint32_t event_key = ((uint32_t)global_sw_id << 16) | (uint32_t)event_id;
    if (event_key == s_last_handled_event_key) {
        return; /* Deduplication: already handled this event */
    }
    s_last_handled_event_key = event_key;
    s_ripple_origin_sw = (int8_t)global_sw_id;
    s_ripple_start_time = k_uptime_get_32();
    s_ripple_hue = hue;
    s_ripple_active = true;
    k_work_reschedule(&s_ripple_work, K_NO_WAIT);
}

/*
 * Linker Wrappers: Extend ZMK native effect cycle from 0..3 to 0..4 (5th effect = Ripple)
 */
int __wrap_zmk_rgb_underglow_calc_effect(int direction) {
    return (s_extended_effect + 5 + direction) % 5;
}

int __wrap_zmk_rgb_underglow_select_effect(int effect) {
    if (effect == 4) {
        s_extended_effect = 4;
        pause_underglow_tick();
        ripple_clear_leds();
        return 0;
    }

    s_extended_effect = effect;
    ripple_cancel_active();
    int ret = __real_zmk_rgb_underglow_select_effect(effect);
    resume_underglow_tick();
    return ret;
}

int __wrap_zmk_rgb_underglow_cycle_effect(int direction) {
    return zmk_rgb_underglow_select_effect(zmk_rgb_underglow_calc_effect(direction));
}

int __wrap_zmk_rgb_underglow_on(void) {
    int ret = __real_zmk_rgb_underglow_on();
    if (s_extended_effect == 4) {
        k_timer_stop(&underglow_tick);
        s_underglow_tick_paused = true;
        ripple_clear_leds();
    }
    return ret;
}

int __wrap_zmk_rgb_underglow_off(void) {
    ripple_cancel_active();
    return __real_zmk_rgb_underglow_off();
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
int __wrap_zmk_split_transport_peripheral_command_handler(
    const struct zmk_split_transport_peripheral *transport,
    struct zmk_split_transport_central_command cmd) {
    if (cmd.type == ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_INVOKE_BEHAVIOR &&
        strncmp(cmd.data.invoke_behavior.behavior_dev, "ripple", 6) == 0) {
        uint8_t origin_sw = (uint8_t)cmd.data.invoke_behavior.param1;
        uint16_t event_id = (uint16_t)(cmd.data.invoke_behavior.param2 >> 16);
        uint16_t hue = (uint16_t)(cmd.data.invoke_behavior.param2 & 0xFFFF);
        ripple_trigger_wave(origin_sw, hue, event_id);
        return 0;
    }
    return __real_zmk_split_transport_peripheral_command_handler(transport, cmd);
}
#endif

/*
 * Workqueue Thread: Renders physical wave at 25 FPS (~40 ms interval)
 */
static void ripple_work_handler(struct k_work *work) {
    if (!s_led_strip || !s_ripple_active) {
        return;
    }

    bool is_on = false;
    if (zmk_rgb_underglow_get_state(&is_on) != 0 || !is_on) {
        s_ripple_active = false;
        ripple_clear_leds();
        return;
    }

    if (s_extended_effect != 4) {
        s_ripple_active = false;
        return;
    }

    uint32_t now = k_uptime_get_32();
    uint32_t elapsed = now - s_ripple_start_time;
    uint32_t duration = CONFIG_NICE_OLED_RIPPLE_DURATION_MS;

    if (elapsed >= duration) {
        s_ripple_active = false;
        s_ripple_origin_sw = -1;
        ripple_clear_leds();
        return;
    }

    int8_t origin = s_ripple_origin_sw;
    if (origin < 0 || origin >= 60) {
        s_ripple_active = false;
        ripple_clear_leds();
        return;
    }

#if THIS_HALF_IS_RIGHT
    uint8_t table_row = (origin < 30) ? (origin + 30) : (origin - 30);
#else
    uint8_t table_row = origin;
#endif

    const uint16_t *dists = cross_dist[table_row];

    /* Wave reaches 350 mm max radius across full keyboard in duration (800 ms) */
    uint32_t wave_r = (elapsed * 350) / duration;
    uint32_t wave_width = 28; /* 28 mm wave crest width */
    uint32_t fade = duration - elapsed;
    uint16_t hue = s_ripple_hue;

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
            s_pixels[i] = hsb_to_rgb(hue, 100, brt);
        } else {
            s_pixels[i] = (struct led_rgb){ .r = 0, .g = 0, .b = 0 };
        }
    }

    led_strip_update_rgb(s_led_strip, s_pixels, 36);

    uint32_t frame_interval_ms = 1000 / CONFIG_NICE_OLED_RIPPLE_FPS;
    if (frame_interval_ms < 15) {
        frame_interval_ms = 15;
    }
    k_work_schedule(&s_ripple_work, K_MSEC(frame_interval_ms));
}

/*
 * Event listener: Only stores keypress position and schedules thread work.
 * ZERO blocking calls, ZERO mutex operations, ZERO SPI access here in ISR context.
 */
static int ripple_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->state) {
        bool is_on = false;
        if (zmk_rgb_underglow_get_state(&is_on) != 0 || !is_on) {
            return 0;
        }

        if (s_extended_effect != 4) {
            return 0;
        }

        if (ev->position >= 60) {
            return 0;
        }
        int8_t global_sw_id = pos_to_global_sw_id[ev->position];
        if (global_sw_id < 0 || global_sw_id >= 60) {
            return 0;
        }

        uint16_t event_id = ++s_local_event_counter;
        uint16_t hue = s_ripple_hue;

        /* Trigger wave on local half */
        ripple_trigger_wave((uint8_t)global_sw_id, hue, event_id);

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        /* Central forwards compact ripple event (8 bytes) to connected peripheral(s) */
        struct zmk_behavior_binding sync_bind = {
            .behavior_dev = "ripple",
            .param1 = (uint32_t)global_sw_id,
            .param2 = ((uint32_t)event_id << 16) | (uint32_t)hue,
        };
        struct zmk_behavior_binding_event sync_ev = {
            .position = ev->position,
            .timestamp = k_uptime_get(),
        };

        for (int i = 0; i < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; i++) {
            if (ev->source != i) {
                zmk_split_central_invoke_behavior(i, &sync_bind, sync_ev, true);
            }
        }
#endif
    }
    return 0;
}

ZMK_LISTENER(ripple_rgb, ripple_listener);
ZMK_SUBSCRIPTION(ripple_rgb, zmk_position_state_changed);

static int ripple_rgb_init(const struct device *dev) {
    s_led_strip = DEVICE_DT_GET(STRIP_CHOSEN);
    if (!device_is_ready(s_led_strip)) {
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

uint16_t ripple_rgb_get_hue(void) {
#if DT_HAS_CHOSEN(zmk_underglow)
    return s_ripple_hue;
#else
    return 0;
#endif
}

void ripple_rgb_set_hue(uint16_t hue) {
#if DT_HAS_CHOSEN(zmk_underglow)
    s_ripple_hue = hue % 360;
#endif
}

void ripple_rgb_change_hue(int direction) {
#if DT_HAS_CHOSEN(zmk_underglow)
    int new_hue = (int)s_ripple_hue + (direction * 15);
    while (new_hue < 0) new_hue += 360;
    s_ripple_hue = (uint16_t)(new_hue % 360);
#endif
}
