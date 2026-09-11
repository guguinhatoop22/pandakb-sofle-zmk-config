/*
 * Copyright (c) 2026 guguinhatoop22
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <string.h>

#include <zmk/split/bluetooth/uuid.h>
#include "dual_role.h"

LOG_MODULE_DECLARE(dual_role, CONFIG_ZMK_LOG_LEVEL);

/* ========================================================================= */
/* Wrapper: bt_le_adv_start                                                 */
/* ========================================================================= */

int __real_bt_le_adv_start(const struct bt_le_adv_param *param,
                           const struct bt_data *ad, size_t ad_len,
                           const struct bt_data *sd, size_t sd_len);

static bool is_split_advertising(const struct bt_data *ad, size_t ad_len) {
    if (ad == NULL || ad_len == 0) {
        /* Directed advertising from peripheral.c passes ad = NULL */
        return true;
    }

    const uint8_t split_uuid_bytes[] = {ZMK_SPLIT_BT_SERVICE_UUID};

    for (size_t i = 0; i < ad_len; i++) {
        if (ad[i].type == BT_DATA_UUID128_ALL || ad[i].type == BT_DATA_UUID128_SOME) {
            if (ad[i].data_len == 16 && memcmp(ad[i].data, split_uuid_bytes, 16) == 0) {
                return true;
            }
        }
    }
    return false;
}

int __wrap_bt_le_adv_start(const struct bt_le_adv_param *param,
                           const struct bt_data *ad, size_t ad_len,
                           const struct bt_data *sd, size_t sd_len) {
    enum dual_role_mode mode = dual_role_get_mode();
    bool is_split = is_split_advertising(ad, ad_len);

    if (mode == DUAL_ROLE_MODE_PERIPHERAL) {
        if (!is_split) {
            LOG_DBG("Suppressing host HID advertising in PERIPHERAL mode");
            return 0;
        }
        LOG_DBG("Allowing split peripheral advertising in PERIPHERAL mode");
        return __real_bt_le_adv_start(param, ad, ad_len, sd, sd_len);
    } else {
        /* CENTRAL mode */
        if (is_split) {
            LOG_DBG("Suppressing split peripheral advertising in CENTRAL mode");
            return 0;
        }
        LOG_DBG("Allowing host HID advertising in CENTRAL mode");
        return __real_bt_le_adv_start(param, ad, ad_len, sd, sd_len);
    }
}

/* ========================================================================= */
/* Wrapper: bt_conn_auth_info_cb_register                                   */
/* ========================================================================= */

int __real_bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb);

static struct bt_conn_auth_info_cb *s_host_auth_cb = NULL;
static struct bt_conn_auth_info_cb s_wrapped_auth_cb;

static void wrapped_pairing_complete(struct bt_conn *conn, bool bonded) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        LOG_DBG("Suppressing ble.c host auth_pairing_complete in PERIPHERAL mode (protecting dongle bond)");
        return;
    }
    if (s_host_auth_cb && s_host_auth_cb->pairing_complete) {
        s_host_auth_cb->pairing_complete(conn, bonded);
    }
}

static void wrapped_pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        LOG_DBG("Suppressing ble.c host auth_pairing_failed in PERIPHERAL mode");
        return;
    }
    if (s_host_auth_cb && s_host_auth_cb->pairing_failed) {
        s_host_auth_cb->pairing_failed(conn, reason);
    }
}

int __wrap_bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb) {
    if (cb == NULL) {
        return -EINVAL;
    }

    /* The first registration comes from ble.c (host authentication) */
    if (s_host_auth_cb == NULL) {
        s_host_auth_cb = cb;
        s_wrapped_auth_cb.pairing_complete = wrapped_pairing_complete;
        s_wrapped_auth_cb.pairing_failed = wrapped_pairing_failed;
        return __real_bt_conn_auth_info_cb_register(&s_wrapped_auth_cb);
    }

    /* Peripheral split authentication registration passes through directly */
    return __real_bt_conn_auth_info_cb_register(cb);
}

/* ========================================================================= */
/* Wrapper: bt_conn_auth_cb_register                                        */
/* ========================================================================= */

int __real_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb);

static const struct bt_conn_auth_cb *s_host_auth_cb_base = NULL;
static struct bt_conn_auth_cb s_wrapped_auth_cb_base;

static enum bt_security_err wrapped_pairing_accept(struct bt_conn *conn,
                                                   const struct bt_conn_pairing_feat *const feat) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        LOG_DBG("Accepting pairing in PERIPHERAL mode for split dongle");
        return BT_SECURITY_ERR_SUCCESS;
    }
    if (s_host_auth_cb_base && s_host_auth_cb_base->pairing_accept) {
        return s_host_auth_cb_base->pairing_accept(conn, feat);
    }
    return BT_SECURITY_ERR_SUCCESS;
}

int __wrap_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb) {
    if (cb == NULL) {
        return -EINVAL;
    }

    if (s_host_auth_cb_base == NULL) {
        s_host_auth_cb_base = cb;
        memcpy(&s_wrapped_auth_cb_base, cb, sizeof(struct bt_conn_auth_cb));
        s_wrapped_auth_cb_base.pairing_accept = wrapped_pairing_accept;
        return __real_bt_conn_auth_cb_register(&s_wrapped_auth_cb_base);
    }

    return __real_bt_conn_auth_cb_register(cb);
}
