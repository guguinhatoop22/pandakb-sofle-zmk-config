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
#include <errno.h>

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
    return -ENOTSUP;
}

/* ========================================================================= */
/* Auth Info Callbacks Wrapping (Neutralize Host Pairing in PERIPHERAL Mode) */
/* ========================================================================= */

int __real_bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb);

#define MAX_AUTH_INFO 4

static struct {
    void (*pairing_complete)(struct bt_conn *conn, bool bonded);
    void (*pairing_failed)(struct bt_conn *conn, enum bt_security_err reason);
    struct bt_conn_auth_info_cb cb;
} s_auth_info[MAX_AUTH_INFO];

static size_t s_auth_info_count = 0;

static void common_pairing_complete(size_t idx, struct bt_conn *conn, bool bonded) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        LOG_DBG("suppress auth_info pairing_complete in PERIPHERAL mode (slot=%u)", (unsigned int)idx);
        return;
    }
    if (idx < s_auth_info_count && s_auth_info[idx].pairing_complete) {
        s_auth_info[idx].pairing_complete(conn, bonded);
    }
}

static void common_pairing_failed(size_t idx, struct bt_conn *conn, enum bt_security_err reason) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        LOG_DBG("suppress auth_info pairing_failed in PERIPHERAL mode (slot=%u, reason=%d)",
                (unsigned int)idx, (int)reason);
        return;
    }
    if (idx < s_auth_info_count && s_auth_info[idx].pairing_failed) {
        s_auth_info[idx].pairing_failed(conn, reason);
    }
}

#define DEFINE_AUTH_INFO_WRAPPER(idx)                                                              \
    static void wrapped_pairing_complete_##idx(struct bt_conn *conn, bool bonded) {                 \
        common_pairing_complete(idx, conn, bonded);                                                 \
    }                                                                                              \
    static void wrapped_pairing_failed_##idx(struct bt_conn *conn, enum bt_security_err reason) {  \
        common_pairing_failed(idx, conn, reason);                                                  \
    }

DEFINE_AUTH_INFO_WRAPPER(0)
DEFINE_AUTH_INFO_WRAPPER(1)
DEFINE_AUTH_INFO_WRAPPER(2)
DEFINE_AUTH_INFO_WRAPPER(3)

static void (*const s_wrapped_complete_fns[])(struct bt_conn *, bool) = {
    wrapped_pairing_complete_0,
    wrapped_pairing_complete_1,
    wrapped_pairing_complete_2,
    wrapped_pairing_complete_3,
};

static void (*const s_wrapped_failed_fns[])(struct bt_conn *, enum bt_security_err) = {
    wrapped_pairing_failed_0,
    wrapped_pairing_failed_1,
    wrapped_pairing_failed_2,
    wrapped_pairing_failed_3,
};

int __wrap_bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb) {
    if (cb == NULL) {
        return -EINVAL;
    }
    if (s_auth_info_count < MAX_AUTH_INFO) {
        size_t idx = s_auth_info_count++;
        s_auth_info[idx].pairing_complete = cb->pairing_complete;
        s_auth_info[idx].pairing_failed = cb->pairing_failed;
        s_auth_info[idx].cb = *cb;
        s_auth_info[idx].cb.pairing_complete = s_wrapped_complete_fns[idx];
        s_auth_info[idx].cb.pairing_failed = s_wrapped_failed_fns[idx];
        return __real_bt_conn_auth_info_cb_register(&s_auth_info[idx].cb);
    }
    LOG_WRN("Exceeded MAX_AUTH_INFO (%d)", MAX_AUTH_INFO);
    return __real_bt_conn_auth_info_cb_register(cb);
}

/* ========================================================================= */
/* Auth Callbacks Wrapping (Just Works in PERIPHERAL, Host in CENTRAL)      */
/* ========================================================================= */

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

static void wrapped_passkey_display(struct bt_conn *conn, unsigned int passkey) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        return;
    }
    if (host_auth_cb && host_auth_cb->passkey_display) {
        host_auth_cb->passkey_display(conn, passkey);
    }
}

static void wrapped_passkey_entry(struct bt_conn *conn) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        return;
    }
    if (host_auth_cb && host_auth_cb->passkey_entry) {
        host_auth_cb->passkey_entry(conn);
    }
}

static void wrapped_passkey_confirm(struct bt_conn *conn, unsigned int passkey) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        return;
    }
    if (host_auth_cb && host_auth_cb->passkey_confirm) {
        host_auth_cb->passkey_confirm(conn, passkey);
    }
}

static void wrapped_cancel(struct bt_conn *conn) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        return;
    }
    if (host_auth_cb && host_auth_cb->cancel) {
        host_auth_cb->cancel(conn);
    }
}

static void wrapped_pairing_confirm(struct bt_conn *conn) {
    if (dual_role_get_mode() == DUAL_ROLE_MODE_PERIPHERAL) {
        return;
    }
    if (host_auth_cb && host_auth_cb->pairing_confirm) {
        host_auth_cb->pairing_confirm(conn);
    }
}

int __wrap_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb) {
    if (cb == NULL) {
        return -EINVAL;
    }
    if (host_auth_cb == NULL) {
        host_auth_cb = cb;
        wrapped_auth_cb = *cb;
        wrapped_auth_cb.pairing_accept = wrapped_pairing_accept;
        wrapped_auth_cb.passkey_display = wrapped_passkey_display;
        wrapped_auth_cb.passkey_entry = wrapped_passkey_entry;
        wrapped_auth_cb.passkey_confirm = wrapped_passkey_confirm;
        wrapped_auth_cb.cancel = wrapped_cancel;
        wrapped_auth_cb.pairing_confirm = wrapped_pairing_confirm;
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
