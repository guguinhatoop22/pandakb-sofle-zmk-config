/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>
#include <fonts.h>
#include <stdio.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS) || !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/events/hid_indicators_changed.h>
#endif

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/modifiers.h>
#endif

#include "right_hud.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static bool caps_active = false;
static bool shift_active = false;
static bool ctrl_active = false;
static bool alt_active = false;
static bool win_active = false;
static uint8_t current_layer = 0;
static const char *layer_names[] = {"BASE", "SYMB", "RUCH", "SYM*", "EDIT", "ADJ"};

static void set_badge_active(lv_obj_t *box, lv_obj_t *lbl, bool active) {
    if (!box || !lbl) return;
    if (active) {
        lv_obj_set_style_bg_color(box, lv_color_white(), 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    } else {
        lv_obj_set_style_bg_color(box, lv_color_black(), 0);
        lv_obj_set_style_border_color(box, lv_color_white(), 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    }
}

static void update_hud_view(struct zmk_widget_right_hud *hud) {
    if (!hud || !hud->obj) return;

    const char *name = (current_layer < 6) ? layer_names[current_layer] : "BASE";
    if (hud->layer_lbl) {
        lv_label_set_text(hud->layer_lbl, name);
    }

    set_badge_active(hud->box_caps, hud->lbl_caps, caps_active);
    set_badge_active(hud->box_sft, hud->lbl_sft, shift_active);
    set_badge_active(hud->box_ctl, hud->lbl_ctl, ctrl_active);
    set_badge_active(hud->box_alt, hud->lbl_alt, alt_active);
    set_badge_active(hud->box_win, hud->lbl_win, win_active);
}

/* --- Caps Lock Listener --- */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS) || !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
struct hud_caps_state {
    bool caps_on;
};

static struct hud_caps_state hud_caps_get_state(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    bool on = (ev != NULL) && ((ev->indicators & 0x02) != 0);
    return (struct hud_caps_state){.caps_on = on};
}

static void hud_caps_update_cb(struct hud_caps_state state) {
    caps_active = state.caps_on;
    struct zmk_widget_right_hud *hud;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, hud, node) {
        update_hud_view(hud);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_hud_caps, struct hud_caps_state, hud_caps_update_cb, hud_caps_get_state)
ZMK_SUBSCRIPTION(widget_hud_caps, zmk_hid_indicators_changed);
#endif

/* --- Battery Listener --- */
struct hud_battery_state {
    uint8_t level;
};

static struct hud_battery_state hud_battery_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    uint8_t level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge();
    return (struct hud_battery_state){.level = level};
}

static void hud_battery_update_cb(struct hud_battery_state state) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d%%", state.level);
    struct zmk_widget_right_hud *hud;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, hud, node) {
        if (hud->batt_lbl) {
            lv_label_set_text(hud->batt_lbl, buf);
        }
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_hud_battery, struct hud_battery_state, hud_battery_update_cb, hud_battery_get_state)
ZMK_SUBSCRIPTION(widget_hud_battery, zmk_battery_state_changed);

/* --- Position Matrix Listener for Local Modifiers & Layer --- */
struct hud_pos_state {
    uint32_t pos;
    bool pressed;
};

static struct hud_pos_state hud_pos_get_state(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    return (struct hud_pos_state){
        .pos = (ev != NULL) ? ev->position : 0,
        .pressed = (ev != NULL) ? ev->state : false,
    };
}

static void hud_pos_update_cb(struct hud_pos_state state) {
    if (state.pos == 23) {
        shift_active = state.pressed;
    } else if (state.pos == 24) {
        current_layer = state.pressed ? 4 : 0;
    } else if (state.pos == 26) {
        ctrl_active = state.pressed;
    } else if (state.pos == 27) {
        alt_active = state.pressed;
    } else if (state.pos == 28) {
        win_active = state.pressed;
    }

    struct zmk_widget_right_hud *hud;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, hud, node) {
        update_hud_view(hud);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_hud_pos, struct hud_pos_state, hud_pos_update_cb, hud_pos_get_state)
ZMK_SUBSCRIPTION(widget_hud_pos, zmk_position_state_changed);

/* --- Central Mode Listener (Native Full Layers & Modifiers) --- */
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
struct hud_central_state {
    uint8_t layer;
    uint8_t mods;
};

static struct hud_central_state hud_central_get_state(const zmk_event_t *eh) {
    uint8_t l = zmk_keymap_highest_layer_active();
    uint8_t m = zmk_hid_get_explicit_mods();
    return (struct hud_central_state){.layer = l, .mods = m};
}

static void hud_central_update_cb(struct hud_central_state state) {
    current_layer = state.layer;
    shift_active = (state.mods & (MOD_LSFT | MOD_RSFT)) != 0;
    ctrl_active = (state.mods & (MOD_LCTL | MOD_RCTL)) != 0;
    alt_active = (state.mods & (MOD_LALT | MOD_RALT)) != 0;
    win_active = (state.mods & (MOD_LGUI | MOD_RGUI)) != 0;

    struct zmk_widget_right_hud *hud;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, hud, node) {
        update_hud_view(hud);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_hud_central, struct hud_central_state, hud_central_update_cb, hud_central_get_state)
ZMK_SUBSCRIPTION(widget_hud_central, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(widget_hud_central, zmk_keycode_state_changed);
#endif

static lv_obj_t *create_badge_box(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_black(), 0);
    lv_obj_set_style_border_color(box, lv_color_white(), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 2, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, x, y);
    return box;
}

static lv_obj_t *create_badge_label(lv_obj_t *box, const char *txt) {
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &pixel_operator_mono_8, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return lbl;
}

int zmk_widget_right_hud_init(struct zmk_widget_right_hud *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 128, 32);
    lv_obj_set_style_bg_color(widget->obj, lv_color_black(), 0);
    lv_obj_set_style_border_width(widget->obj, 0, 0);
    lv_obj_set_style_radius(widget->obj, 0, 0);
    lv_obj_set_style_pad_all(widget->obj, 0, 0);
    lv_obj_set_scrollbar_mode(widget->obj, LV_SCROLLBAR_MODE_OFF);

    // Title: GUGUINHATOP
    widget->title = lv_label_create(widget->obj);
    lv_label_set_text(widget->title, "GUGUINHATOP");
    lv_obj_set_style_text_font(widget->title, &pixel_operator_mono_12, 0);
    lv_obj_set_style_text_color(widget->title, lv_color_white(), 0);
    lv_obj_align(widget->title, LV_ALIGN_TOP_LEFT, 2, 0);

    // Battery percentage
    widget->batt_lbl = lv_label_create(widget->obj);
    lv_label_set_text(widget->batt_lbl, "100%");
    lv_obj_set_style_text_font(widget->batt_lbl, &pixel_operator_mono_12, 0);
    lv_obj_set_style_text_color(widget->batt_lbl, lv_color_white(), 0);
    lv_obj_align(widget->batt_lbl, LV_ALIGN_TOP_RIGHT, -2, 0);

    // Divider Line (Y=14)
    lv_obj_t *line = lv_obj_create(widget->obj);
    lv_obj_set_size(line, 128, 1);
    lv_obj_set_style_bg_color(line, lv_color_white(), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 14);

    // Layer Badge (Y=16, X=1, 36x15)
    widget->layer_box = lv_obj_create(widget->obj);
    lv_obj_set_size(widget->layer_box, 36, 15);
    lv_obj_set_style_bg_color(widget->layer_box, lv_color_white(), 0);
    lv_obj_set_style_border_width(widget->layer_box, 0, 0);
    lv_obj_set_style_radius(widget->layer_box, 2, 0);
    lv_obj_set_style_pad_all(widget->layer_box, 0, 0);
    lv_obj_set_scrollbar_mode(widget->layer_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(widget->layer_box, LV_ALIGN_TOP_LEFT, 1, 16);

    widget->layer_lbl = lv_label_create(widget->layer_box);
    lv_label_set_text(widget->layer_lbl, "BASE");
    lv_obj_set_style_text_font(widget->layer_lbl, &pixel_operator_mono_12, 0);
    lv_obj_set_style_text_color(widget->layer_lbl, lv_color_black(), 0);
    lv_obj_center(widget->layer_lbl);

    // Five Badges (Y=16, 16x15 each, starting at X=39)
    widget->box_caps = create_badge_box(widget->obj, 39, 16, 16, 15);
    widget->lbl_caps = create_badge_label(widget->box_caps, "CAP");

    widget->box_sft = create_badge_box(widget->obj, 57, 16, 16, 15);
    widget->lbl_sft = create_badge_label(widget->box_sft, "SFT");

    widget->box_ctl = create_badge_box(widget->obj, 75, 16, 16, 15);
    widget->lbl_ctl = create_badge_label(widget->box_ctl, "CTL");

    widget->box_alt = create_badge_box(widget->obj, 93, 16, 16, 15);
    widget->lbl_alt = create_badge_label(widget->box_alt, "ALT");

    widget->box_win = create_badge_box(widget->obj, 111, 16, 16, 15);
    widget->lbl_win = create_badge_label(widget->box_win, "WIN");

    update_hud_view(widget);

    sys_slist_append(&widgets, &widget->node);

    widget_hud_battery_init();
    widget_hud_pos_init();

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS) || !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    widget_hud_caps_init();
#endif

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    widget_hud_central_init();
#endif

    return 0;
}

lv_obj_t *zmk_widget_right_hud_obj(struct zmk_widget_right_hud *widget) {
    return widget->obj;
}
