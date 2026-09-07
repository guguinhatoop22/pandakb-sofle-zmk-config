/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_right_hud {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *title;
    lv_obj_t *batt_lbl;
    lv_obj_t *layer_box;
    lv_obj_t *layer_lbl;
    lv_obj_t *box_caps;
    lv_obj_t *lbl_caps;
    lv_obj_t *box_sft;
    lv_obj_t *lbl_sft;
    lv_obj_t *box_ctl;
    lv_obj_t *lbl_ctl;
    lv_obj_t *box_alt;
    lv_obj_t *lbl_alt;
    lv_obj_t *box_win;
    lv_obj_t *lbl_win;
};

int zmk_widget_right_hud_init(struct zmk_widget_right_hud *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_right_hud_obj(struct zmk_widget_right_hud *widget);
