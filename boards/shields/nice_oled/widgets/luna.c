/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>
#endif

#include "luna.h"

#define SRC(array) (const void **)array, sizeof(array) / sizeof(lv_img_dsc_t *)

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

LV_IMG_DECLARE(dog_sit1);
LV_IMG_DECLARE(dog_sit2);
LV_IMG_DECLARE(dog_walk1);
LV_IMG_DECLARE(dog_walk2);
LV_IMG_DECLARE(dog_run1);
LV_IMG_DECLARE(dog_run2);
LV_IMG_DECLARE(dog_sneak1);
LV_IMG_DECLARE(dog_sneak2);

#define ANIMATION_SPEED_IDLE 960
const lv_img_dsc_t *idle_imgs[] = {
    &dog_sit1,
    &dog_sit2,
};

#define ANIMATION_SPEED_SLOW 200
const lv_img_dsc_t *slow_imgs[] = {
    &dog_walk1,
    &dog_walk2,
};

#define ANIMATION_SPEED_MID 200
const lv_img_dsc_t *mid_imgs[] = {
    &dog_walk1,
    &dog_walk2,
};

#define ANIMATION_SPEED_FAST 200
const lv_img_dsc_t *fast_imgs[] = {
    &dog_run1,
    &dog_run2,
};

struct luna_wpm_status_state {
    uint8_t wpm;
};

enum anim_state {
    anim_state_none,
    anim_state_idle,
    anim_state_slow,
    anim_state_mid,
    anim_state_fast
} current_anim_state;

static uint32_t last_tap_time = 0;
static lv_timer_t *luna_idle_timer = NULL;

static void set_animation(lv_obj_t *animing, struct luna_wpm_status_state state) {
    if (state.wpm < 15) {
        if (current_anim_state != anim_state_idle) {
            lv_animimg_set_src(animing, SRC(idle_imgs));
            lv_animimg_set_duration(animing, ANIMATION_SPEED_IDLE);
            lv_animimg_set_repeat_count(animing, LV_ANIM_REPEAT_INFINITE);
            lv_animimg_start(animing);
            current_anim_state = anim_state_idle;
        }
    } else if (state.wpm < 30) {
        if (current_anim_state != anim_state_slow) {
            lv_animimg_set_src(animing, SRC(slow_imgs));
            lv_animimg_set_duration(animing, ANIMATION_SPEED_SLOW);
            lv_animimg_set_repeat_count(animing, LV_ANIM_REPEAT_INFINITE);
            lv_animimg_start(animing);
            current_anim_state = anim_state_slow;
        }
    } else if (state.wpm < 70) {
        if (current_anim_state != anim_state_mid) {
            lv_animimg_set_src(animing, SRC(mid_imgs));
            lv_animimg_set_duration(animing, ANIMATION_SPEED_MID);
            lv_animimg_set_repeat_count(animing, LV_ANIM_REPEAT_INFINITE);
            lv_animimg_start(animing);
            current_anim_state = anim_state_mid;
        }
    } else {
        if (current_anim_state != anim_state_fast) {
            lv_animimg_set_src(animing, SRC(fast_imgs));
            lv_animimg_set_duration(animing, ANIMATION_SPEED_FAST);
            lv_animimg_set_repeat_count(animing, LV_ANIM_REPEAT_INFINITE);
            lv_animimg_start(animing);
            current_anim_state = anim_state_fast;
        }
    }
}

static void luna_idle_timer_cb(lv_timer_t *timer) {
    uint32_t now = k_uptime_get_32();
    if (last_tap_time > 0 && (now - last_tap_time >= 1200) && current_anim_state != anim_state_idle) {
        struct zmk_widget_luna *widget;
        SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
            set_animation(widget->obj, (struct luna_wpm_status_state){.wpm = 0});
        }
    }
}

static struct luna_wpm_status_state luna_position_get_state(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    uint8_t wpm = 0;
    if (ev && ev->state) {
        uint32_t now = k_uptime_get_32();
        uint32_t diff = (last_tap_time == 0) ? 1000 : (now - last_tap_time);
        last_tap_time = now;

        if (diff < 220) {
            wpm = 80;
        } else if (diff < 450) {
            wpm = 50;
        } else {
            wpm = 25;
        }
    }
    return (struct luna_wpm_status_state){.wpm = wpm};
}

static void luna_position_update_cb(struct luna_wpm_status_state state) {
    if (state.wpm > 0) {
        struct zmk_widget_luna *widget;
        SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
            set_animation(widget->obj, state);
        }
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_luna_position, struct luna_wpm_status_state, luna_position_update_cb,
                            luna_position_get_state)
ZMK_SUBSCRIPTION(widget_luna_position, zmk_position_state_changed);

#if IS_ENABLED(CONFIG_ZMK_WPM)
struct luna_wpm_status_state luna_wpm_status_get_state(const zmk_event_t *eh) {
    struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    return (struct luna_wpm_status_state){.wpm = ev->state};
};

void luna_wpm_status_update_cb(struct luna_wpm_status_state state) {
    struct zmk_widget_luna *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_animation(widget->obj, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_luna, struct luna_wpm_status_state, luna_wpm_status_update_cb,
                            luna_wpm_status_get_state)

ZMK_SUBSCRIPTION(widget_luna, zmk_wpm_state_changed);
#endif

int zmk_widget_luna_init(struct zmk_widget_luna *widget, lv_obj_t *parent) {
    widget->obj = lv_animimg_create(parent);
    lv_obj_center(widget->obj);

    if (luna_idle_timer == NULL) {
        luna_idle_timer = lv_timer_create(luna_idle_timer_cb, 200, NULL);
    }

    set_animation(widget->obj, (struct luna_wpm_status_state){.wpm = 0});

    sys_slist_append(&widgets, &widget->node);

    widget_luna_position_init();

#if IS_ENABLED(CONFIG_ZMK_WPM)
    widget_luna_init();
#endif

    return 0;
}

lv_obj_t *zmk_widget_luna_obj(struct zmk_widget_luna *widget) { return widget->obj; }
