/*
 * Copyright (c) 2026 guguinhatoop22
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

enum dual_role_mode {
    DUAL_ROLE_MODE_PERIPHERAL = 0,
    DUAL_ROLE_MODE_CENTRAL = 1,
};

enum dual_role_mode dual_role_get_mode(void);
bool dual_role_is_internal_adv_call(void);
void dual_role_set_internal_adv_call(bool internal);

bool dual_role_is_dongle_connected(void);

typedef void (*dual_role_mode_changed_cb_t)(enum dual_role_mode mode);
void dual_role_register_mode_callback(dual_role_mode_changed_cb_t cb);
