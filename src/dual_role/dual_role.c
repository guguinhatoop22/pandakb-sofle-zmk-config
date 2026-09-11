/*
 * Copyright (c) 2026 guguinhatoop22
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/transport/central.h>
#include <zmk/split/transport/peripheral.h>
#include <zmk/split/bluetooth/peripheral.h>

#include "dual_role.h"

LOG_MODULE_REGISTER(dual_role, CONFIG_ZMK_LOG_LEVEL);

static enum dual_role_mode s_mode = DUAL_ROLE_MODE_PERIPHERAL;
static bool s_internal_adv_call = false;

enum dual_role_mode dual_role_get_mode(void) {
    return s_mode;
}

bool dual_role_is_internal_adv_call(void) {
    return s_internal_adv_call;
}

void dual_role_set_internal_adv_call(bool internal) {
    s_internal_adv_call = internal;
}

/* ========================================================================= */
/* C1: Priority 0 Stub Transports                                            */
/* ========================================================================= */

static zmk_split_transport_central_status_changed_cb_t s_central_status_cb;
static zmk_split_transport_peripheral_status_changed_cb_t s_peripheral_status_cb;

static bool s_central_stub_enabled = false;
static bool s_peripheral_stub_enabled = false;
/* When true, peripheral stub briefly claims "available" so ZMK tears down and
 * re-selects bt_peripheral (used to re-arm split advertising after demotion). */
static bool s_kick_peripheral = false;

/* Stub Central:
 * When in PERIPHERAL mode (dongle mode): available = true.
 * ZMK Split Central selects this priority-0 stub, keeping real bt_central disabled.
 * When promoted to CENTRAL mode: available = false.
 * ZMK Split Central skips this stub and enables real bt_central.
 */
static int stub_central_send_command(uint8_t source, struct zmk_split_transport_central_command cmd) {
    ARG_UNUSED(source);
    ARG_UNUSED(cmd);
    return -ENODEV;
}

static int stub_central_get_available_source_ids(uint8_t *sources) {
    ARG_UNUSED(sources);
    return 0;
}

static int stub_central_set_enabled(bool en) {
    s_central_stub_enabled = en;
    return 0;
}

static struct zmk_split_transport_status stub_central_get_status(void) {
    return (struct zmk_split_transport_status){
        .available = (s_mode == DUAL_ROLE_MODE_PERIPHERAL),
        .enabled = s_central_stub_enabled,
        .connections = ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_DISCONNECTED,
    };
}

static int stub_central_set_status_callback(zmk_split_transport_central_status_changed_cb_t cb) {
    s_central_status_cb = cb;
    return 0;
}

static const struct zmk_split_transport_central_api stub_central_api = {
    .send_command = stub_central_send_command,
    .get_available_source_ids = stub_central_get_available_source_ids,
    .set_enabled = stub_central_set_enabled,
    .get_status = stub_central_get_status,
    .set_status_callback = stub_central_set_status_callback,
};

ZMK_SPLIT_TRANSPORT_CENTRAL_REGISTER(dual_role_central, &stub_central_api, 0);

/* Stub Peripheral:
 * When in CENTRAL mode (USB direct mode): available = true.
 * ZMK Split Peripheral selects this priority-0 stub, keeping real bt_peripheral disabled.
 * When in PERIPHERAL mode (dongle mode): available = false.
 * ZMK Split Peripheral skips this stub and enables real bt_peripheral.
 */
static int stub_peripheral_report_event(const struct zmk_split_transport_peripheral_event *event) {
    ARG_UNUSED(event);
    return 0;
}

static int stub_peripheral_set_enabled(bool en) {
    s_peripheral_stub_enabled = en;
    return 0;
}

static struct zmk_split_transport_status stub_peripheral_get_status(void) {
    return (struct zmk_split_transport_status){
        .available = (s_mode == DUAL_ROLE_MODE_CENTRAL) || s_kick_peripheral,
        .enabled = s_peripheral_stub_enabled,
        .connections = ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_DISCONNECTED,
    };
}

static int stub_peripheral_set_status_callback(zmk_split_transport_peripheral_status_changed_cb_t cb) {
    s_peripheral_status_cb = cb;
    return 0;
}

static const struct zmk_split_transport_peripheral_api stub_peripheral_api = {
    .report_event = stub_peripheral_report_event,
    .set_enabled = stub_peripheral_set_enabled,
    .get_status = stub_peripheral_get_status,
    .set_status_callback = stub_peripheral_set_status_callback,
};

ZMK_SPLIT_TRANSPORT_PERIPHERAL_REGISTER(dual_role_peripheral, &stub_peripheral_api, 0);

/* ========================================================================= */
/* State Transitions (Promotion / Demotion) with C2 Safety                   */
/* ========================================================================= */

static void count_le_conns_cb(struct bt_conn *conn, void *data) {
    int *count = (int *)data;
    (*count)++;
}

/* Drop every LE connection. Used on mode changes so a leftover host-HID or
 * split link cannot keep the controller busy and block the next role's
 * advertising/scanning (this was the field failure: after USB demotion the
 * host HID advert from ble.c was still active, so split adv never restarted
 * and the dongle could not reconnect; the phone kept seeing the Left). */
static void disconnect_le_conn_cb(struct bt_conn *conn, void *data) {
    ARG_UNUSED(data);
    int err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err && err != -ENOTCONN) {
        LOG_WRN("disconnect LE failed (%d)", err);
    }
}

static void purge_le_radio(void) {
    int err = bt_le_adv_stop();
    if (err && err != -EALREADY) {
        LOG_WRN("bt_le_adv_stop on transition: %d", err);
    }
    bt_conn_foreach(BT_CONN_TYPE_LE, disconnect_le_conn_cb, NULL);
}

static void trigger_transition(void) {
    /* Always free the radio before asking ZMK to switch transports. */
    purge_le_radio();

    if (s_mode == DUAL_ROLE_MODE_CENTRAL) {
        LOG_INF("Promoting: disabling peripheral stub, enabling central bt");
        /* C2: Disable peripheral first (peripheral stub becomes available, ZMK switches off bt_peripheral) */
        if (s_peripheral_status_cb) {
            s_peripheral_status_cb(&dual_role_peripheral, stub_peripheral_get_status());
        }
        /* Then enable central (central stub becomes unavailable, ZMK switches on bt_central) */
        if (s_central_status_cb) {
            s_central_status_cb(&dual_role_central, stub_central_get_status());
        }
    } else {
        LOG_INF("Demoting: disabling central bt, enabling peripheral bt");
        /* C2: Disable central first (central stub becomes available, ZMK switches off bt_central) */
        if (s_central_status_cb) {
            s_central_status_cb(&dual_role_central, stub_central_get_status());
        }
        /* Then enable peripheral (peripheral stub becomes unavailable, ZMK switches on bt_peripheral) */
        if (s_peripheral_status_cb) {
            s_peripheral_status_cb(&dual_role_peripheral, stub_peripheral_get_status());
        }
    }
}

/* After demotion, ble.c may still race and call bt_le_adv_stop on what it
 * thinks is "its" host advert — which can kill the split peripheral advert
 * we just started. Briefly claim the peripheral stub then release it so ZMK
 * re-enables bt_peripheral and starts advertising again. */
static void evaluate_state(void);
static void kick_split_adv_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_kick_split_adv_work, kick_split_adv_work_cb);

static void kick_split_adv_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    /* Also re-check USB/dongle in case the cable was already plugged at boot
     * (usb_conn_state_changed may have fired before we subscribed). */
    evaluate_state();

    if (s_mode != DUAL_ROLE_MODE_PERIPHERAL || !s_peripheral_status_cb) {
        return;
    }
    LOG_INF("Re-arming split peripheral advertising after demotion/boot");
    (void)bt_le_adv_stop();
    s_kick_peripheral = true;
    s_peripheral_status_cb(&dual_role_peripheral, stub_peripheral_get_status());
    s_kick_peripheral = false;
    s_peripheral_status_cb(&dual_role_peripheral, stub_peripheral_get_status());
}

static void demote_to_peripheral(void) {
    if (s_mode == DUAL_ROLE_MODE_PERIPHERAL) {
        return;
    }
    LOG_INF("Demoting Left to PERIPHERAL mode (Dongle mode)");
    s_mode = DUAL_ROLE_MODE_PERIPHERAL;
    trigger_transition();
    k_work_reschedule(&s_kick_split_adv_work, K_MSEC(100));
}

static void promotion_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_promotion_work, promotion_work_cb);

static void promotion_work_cb(struct k_work *work) {
    /* Verify conditions for promotion */
    if (zmk_usb_get_conn_state() != ZMK_USB_CONN_HID) {
        LOG_DBG("Promotion aborted: USB is not in HID state");
        return;
    }

    if (zmk_split_bt_peripheral_is_connected()) {
        LOG_DBG("Promotion aborted: Dongle is connected to peripheral");
        return;
    }

    /* C2: Never disconnect arbitrary LE connections. If > 1 active LE connection, defer promotion */
    int active_conns = 0;
    bt_conn_foreach(BT_CONN_TYPE_LE, count_le_conns_cb, &active_conns);
    if (active_conns > 1) {
        LOG_WRN("Active LE connections count is %d (> 1). Deferring promotion for safety.", active_conns);
        k_work_reschedule(&s_promotion_work, K_MSEC(CONFIG_SOFLE_DUAL_ROLE_PROMOTE_DELAY_MS));
        return;
    }

    if (s_mode == DUAL_ROLE_MODE_CENTRAL) {
        return;
    }

    LOG_INF("Conditions met. Promoting Left to CENTRAL mode (USB Direct mode)");
    s_mode = DUAL_ROLE_MODE_CENTRAL;
    trigger_transition();
}

static void evaluate_state(void) {
    enum zmk_usb_conn_state usb_state = zmk_usb_get_conn_state();
    bool dongle_connected = zmk_split_bt_peripheral_is_connected();

    LOG_DBG("Evaluate state: USB=%d, DongleConnected=%d, CurrentMode=%d",
            usb_state, dongle_connected, s_mode);

    if (usb_state == ZMK_USB_CONN_HID && !dongle_connected) {
        if (s_mode == DUAL_ROLE_MODE_PERIPHERAL) {
            LOG_INF("USB HID detected and Dongle absent. Scheduling promotion in %d ms",
                    CONFIG_SOFLE_DUAL_ROLE_PROMOTE_DELAY_MS);
            k_work_reschedule(&s_promotion_work, K_MSEC(CONFIG_SOFLE_DUAL_ROLE_PROMOTE_DELAY_MS));
        }
    } else {
        /* If USB is disconnected / power only, or Dongle connects, cancel any pending promotion */
        k_work_cancel_delayable(&s_promotion_work);

        if (s_mode == DUAL_ROLE_MODE_CENTRAL) {
            if (usb_state != ZMK_USB_CONN_HID || dongle_connected) {
                demote_to_peripheral();
            }
        }
    }
}

/* ========================================================================= */
/* Event Listeners & Local Keymap Blocking                                   */
/* ========================================================================= */

static int dual_role_event_listener(const zmk_event_t *eh) {
    if (as_zmk_usb_conn_state_changed(eh) != NULL ||
        as_zmk_split_peripheral_status_changed(eh) != NULL) {
        evaluate_state();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dual_role_events, dual_role_event_listener);
ZMK_SUBSCRIPTION(dual_role_events, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(dual_role_events, zmk_split_peripheral_status_changed);

/* In PERIPHERAL mode, block position state changes from reaching keymap.c */
static int dual_role_position_listener(const zmk_event_t *eh) {
    if (as_zmk_position_state_changed(eh) != NULL) {
        if (s_mode == DUAL_ROLE_MODE_PERIPHERAL) {
            /* Keystrokes are forwarded to Dongle by split_peripheral listener;
             * swallow event here so local keymap engine does not process them */
            return ZMK_EV_EVENT_HANDLED;
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dual_role_position, dual_role_position_listener);
ZMK_SUBSCRIPTION(dual_role_position, zmk_position_state_changed);

/* ========================================================================= */
/* C4: Runtime Order Verification                                            */
/* ========================================================================= */

extern struct zmk_event_subscription __event_subscriptions_start[];
extern struct zmk_event_subscription __event_subscriptions_end[];
extern const struct zmk_listener zmk_listener_split_peripheral;
extern const struct zmk_listener zmk_listener_dual_role_position;
extern const struct zmk_listener zmk_listener_keymap;

static int dual_role_init(void) {
    LOG_INF("Initializing Sofle Dual Role module (default mode: PERIPHERAL)");

    /* C4 runtime verification: verify split_peripheral < dual_role_position < keymap */
    int split_peripheral_idx = -1;
    int dual_role_idx = -1;
    int keymap_idx = -1;
    size_t count = __event_subscriptions_end - __event_subscriptions_start;

    for (size_t i = 0; i < count; i++) {
        if (__event_subscriptions_start[i].listener == &zmk_listener_split_peripheral) {
            split_peripheral_idx = (int)i;
        } else if (__event_subscriptions_start[i].listener == &zmk_listener_dual_role_position) {
            dual_role_idx = (int)i;
        } else if (__event_subscriptions_start[i].listener == &zmk_listener_keymap) {
            keymap_idx = (int)i;
        }
    }

    if (split_peripheral_idx >= 0 && dual_role_idx >= 0 && keymap_idx >= 0) {
        if (split_peripheral_idx < dual_role_idx && dual_role_idx < keymap_idx) {
            LOG_INF("C4 VERIFIED: subscription order split_peripheral (%d) < dual_role (%d) < keymap (%d)",
                    split_peripheral_idx, dual_role_idx, keymap_idx);
        } else {
            LOG_ERR("C4 CRITICAL: invalid listener order: sp=%d, dr=%d, km=%d! Keystrokes may be dropped or duplicated.",
                    split_peripheral_idx, dual_role_idx, keymap_idx);
        }
    } else {
        LOG_WRN("C4 WARNING: could not locate all 3 listener subscriptions (sp=%d, dr=%d, km=%d)",
                split_peripheral_idx, dual_role_idx, keymap_idx);
    }

    /* Settings + USB enumeration finish after SYS_INIT. Re-evaluate and make
     * sure split advertising is open so the dongle can find us on a cold boot
     * with cable already plugged (or after a previous host-HID session). */
    k_work_reschedule(&s_kick_split_adv_work, K_MSEC(1500));
    return 0;
}

/* Run in APPLICATION level after basic kernel initialization */
SYS_INIT(dual_role_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
