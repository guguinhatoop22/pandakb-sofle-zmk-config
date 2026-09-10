/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>

void ripple_rgb_init_widget(void);

uint16_t ripple_rgb_get_hue(void);
void ripple_rgb_set_hue(uint16_t hue);
void ripple_rgb_change_hue(int direction);
