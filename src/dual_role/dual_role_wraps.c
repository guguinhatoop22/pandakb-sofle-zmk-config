/*
 * Copyright (c) 2026 guguinhatoop22
 * SPDX-License-Identifier: MIT
 *
 * Linker's --wrap hooks for the hybrid Left.
 *
 * Advertising policy (field-test fix):
 *   - PERIPHERAL mode: only allow split-service advertising (dongle can find us).
 *   - CENTRAL mode: allow no advertising at all. USB is the host HID path; BLE
 *     is only used as central to the Right. Host HID advertising was what made
 *     the phone discover the Left and also blocked split re-advertising after
 *     demotion (bt_le_adv_start failed because host adv was still active).
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include <zmk/split/bluetooth/uuid.h>

#include "dual_role.h"

LOG_MODULE_DECLARE(dual_role, CONFIG_ZMK_LOG_LEVEL);

int __real_bt_le_adv_start(const struct bt_le_adv_param *param, const struct bt_data *ad,
                           size_t ad_len, const struct bt_data *sd, size_t sd_len);

static bool is_split_advertising(const struct bt_data *ad, size_t ad_len) {
    /* Split directed advertising passes ad=NULL (peripheral.c). */
    if (ad == NULL || ad_len == 0) {
        return true;
    }

    const uint8_t split_uuid[] = {ZMK_SPLIT_BT_SERVICE_UUID};
    for (size_t i = 0; i < ad_len; i++) {
        if ((ad[i].type == BT_DATA_UUID128_ALL || ad[i].type == BT_DATA_UUID128_SOME) &&
            ad[i].data_len == 16 && memcmp(ad[i].data, split_uuid, 16) == 0) {
            return true;
        }
    }
    return false;
}

int __wrap_bt_le_adv_start(const struct bt_le_adv_param *param, const struct bt_data *ad,
                           size_t ad_len, const struct bt_data *sd, size_t sd_len) {
    const enum dual_role_mode mode = dual_role_get_mode();
    const bool split = is_split_advertising(ad, ad_len);

    /* Only the split peripheral role may advertise, and only while we are the
     * dongle's peripheral. Host HID advertising is always suppressed: USB is
     * the host path when promoted; the dongle is the host when demoted. */
    if (mode == DUAL_ROLE_MODE_PERIPHERAL && split) {
        return __real_bt_le_adv_start(param, ad, ad_len, sd, sd_len);
    }

    LOG_DBG("suppress adv (mode=%d split=%d)", (int)mode, (int)split);
    return 0;
}

int __real_bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb);

static struct bt_conn_auth_info_cb *host_auth_info;
static struct bt_conn_auth_info_cb wrapped_auth_info;

static void wrapped_pairing_complete(struct bt_conn *conn, bool bonded) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        LOG_DBG("ignore host pairing_complete in peripheral mode");
        return;
    }
    if (host_auth_info && host_auth_info->pairing_complete) {
        host_auth_info->pairing_complete(conn, bonded);
    }
}

static void wrapped_pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        return;
    }
    if (host_auth_info && host_auth_info->pairing_failed) {
        host_auth_info->pairing_failed(conn, reason);
    }
}

int __wrap_bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb) {
    if (cb == NULL) {
        return -EINVAL;
    }
    if (host_auth_info == NULL) {
        host_auth_info = cb;
        wrapped_auth_info = *cb;
        wrapped_auth_info.pairing_complete = wrapped_pairing_complete;
        wrapped_auth_info.pairing_failed = wrapped_pairing_failed;
        return __real_bt_conn_auth_info_cb_register(&wrapped_auth_info);
    }
    return __real_bt_conn_auth_info_cb_register(cb);
}

int __real_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb);

static const struct bt_conn_auth_cb *host_auth_cb;
static struct bt_conn_auth_cb wrapped_auth_cb;

static enum bt_security_err wrapped_pairing_accept(struct bt_conn *conn,
                                                   const struct bt_conn_pairing_feat *const feat) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        return BT_SECURITY_ERR_SUCCESS;
    }
    if (host_auth_cb && host_auth_cb->pairing_accept) {
        return host_auth_cb->pairing_accept(conn, feat);
    }
    return BT_SECURITY_ERR_SUCCESS;
}

int __wrap_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb) {
    if (cb == NULL) {
        return -EINVAL;
    }
    if (host_auth_cb == NULL) {
        host_auth_cb = cb;
        wrapped_auth_cb = *cb;
        wrapped_auth_cb.pairing_accept = wrapped_pairing_accept;
        return __real_bt_conn_auth_cb_register(&wrapped_auth_cb);
    }
    return __real_bt_conn_auth_cb_register(cb);
}

/*
 * Force undirected open advertising for the split peripheral role on the hybrid
 * Left. Without this, peripheral.c directed-advertises to the last bond — which
 * after the failed USB test was often the phone — so the dongle never reconnects.
 * Same technique as right_open_adv.c on the Right half.
 */
void __real_bt_foreach_bond(uint8_t id,
                            void (*func)(const struct bt_bond_info *, void *), void *user_data);

void __wrap_bt_foreach_bond(uint8_t id,
                            void (*func)(const struct bt_bond_info *, void *), void *user_data) {
    ARG_UNUSED(id);
    ARG_UNUSED(func);
    ARG_UNUSED(user_data);
}
