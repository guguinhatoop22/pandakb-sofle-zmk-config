/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>

#define REACTIVE_RGB_PEAK_BRT 80
#define REACTIVE_RGB_IDLE_BRT 0
#define REACTIVE_RGB_DECAY_STEP 4
#define REACTIVE_RGB_HUE_STEP 14

static uint32_t last_tap_time = 0;
static uint16_t current_hue = 0;
static uint8_t current_brt = 0;
static bool reactive_active = false;

static void decay_timer_cb(struct k_timer *timer);
K_TIMER_DEFINE(decay_timer, decay_timer_cb, NULL);

static void decay_timer_cb(struct k_timer *timer) {
    uint32_t now = k_uptime_get_32();
    if (last_tap_time > 0 && (now - last_tap_time > 150)) {
        if (current_brt > REACTIVE_RGB_IDLE_BRT) {
            if (current_brt > REACTIVE_RGB_DECAY_STEP) {
                current_brt -= REACTIVE_RGB_DECAY_STEP;
            } else {
                current_brt = REACTIVE_RGB_IDLE_BRT;
            }
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){
                .h = current_hue,
                .s = 100,
                .b = current_brt,
            });
        } else {
            k_timer_stop(&decay_timer);
            reactive_active = false;
        }
    }
}

static int reactive_rgb_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->state) {
        last_tap_time = k_uptime_get_32();
        current_brt = REACTIVE_RGB_PEAK_BRT;
        current_hue = (current_hue + REACTIVE_RGB_HUE_STEP) % 360;

        bool is_on = false;
        zmk_rgb_underglow_get_state(&is_on);
        if (!is_on) {
            zmk_rgb_underglow_on();
        }

        zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){
            .h = current_hue,
            .s = 100,
            .b = current_brt,
        });

        if (!reactive_active) {
            reactive_active = true;
            k_timer_start(&decay_timer, K_MSEC(40), K_MSEC(40));
        }
    }
    return 0;
}

ZMK_LISTENER(reactive_rgb, reactive_rgb_listener);
ZMK_SUBSCRIPTION(reactive_rgb, zmk_position_state_changed);
