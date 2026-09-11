/*
 * Copyright (c) 2026 guguinhatoop22
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

void __real_bt_foreach_bond(uint8_t id,
                            void (*func)(const struct bt_bond_info *, void *), void *user_data);

/*
 * start_advertising() in peripheral.c:59-73 only uses directed advertising if the
 * foreach_bond callback receives a non-empty address. By not invoking the callback,
 * it falls through to undirected advertising with the split UUID, discoverable by
 * both the Dongle Central and the Left Central.
 */
void __wrap_bt_foreach_bond(uint8_t id,
                            void (*func)(const struct bt_bond_info *, void *), void *user_data) {
    ARG_UNUSED(id);
    ARG_UNUSED(func);
    ARG_UNUSED(user_data);
}
