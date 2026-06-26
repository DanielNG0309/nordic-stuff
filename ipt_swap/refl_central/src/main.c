/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Channel Sounding Reflector with Inline PCT Transfer - BLE-central hub.
 *
 *  ROLE SWAP: this CS reflector is the BLE CENTRAL. It scans for and connects to
 *  up to CONFIG_BT_MAX_CONN peripheral CS initiators, and drives the full CS setup
 *  for each link (ACL security -> CS capabilities -> IPT config(role=REFLECTOR) ->
 *  CS security). Each peripheral-initiator then enables procedures and computes its
 *  own IFFT distance locally (no GATT/RAS transfer).
 *
 *  Being the central is the whole point of the multi-link variant: the central can
 *  SPACE the links (CONFIG_BT_CTLR_SDC_CENTRAL_ACL_EVENT_SPACING_DEFAULT) with a
 *  uniform connection interval so the per-link CS subevents do not collide - the
 *  collision control a peripheral-reflector structurally cannot do.
 */

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/scan.h>
#include <dk_buttons_and_leds.h>

/* CS-Turn service (hosted by each peripheral-initiator). The central writes 1 ("GO") to
 * grant a turn and gets a 1 ("DONE") notification when that link's burst completes. The
 * char value + CCC handles are fixed (see CS_TURN_VAL_HANDLE below). */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

#define CON_STATUS_LED DK_LED1
#define INITIATOR_NAME "Nordic CS IPT Initiator"

/* Per-link connection interval (units of 1.25 ms). UNIFORM across links: with round-robin
 * only one link ranges at a time, so there is no radio contention to de-correlate. Equal
 * intervals keep the central's links at a FIXED offset from each other (ACL event spacing)
 * forever, so they never collide - whereas DISTINCT intervals make the anchors drift, and a
 * link's 5 ms CS subevent periodically clobbers another link's ACL event, aborting its
 * procedures and starving it. (De-correlation was the right tool for the free-running
 * attempt; it is the wrong tool once round-robin removes the contention.)
 *
 * The interval must hold all N link anchors (spaced 6.5 ms) PLUS the active link's 5 ms CS
 * subevent with margin. 20 ms proved too tight for 3 links (slot 0's CS couldn't get
 * scheduled and timed out); 30 ms gives the schedule room for all three to range. Smaller
 * intervals raise the rate but need fewer links or a shorter CS subevent. */
#define CONN_INTERVAL_BASE 24
#define CONN_INTERVAL_STEP 0
#define CONN_INTERVAL_FOR_SLOT(slot) (CONN_INTERVAL_BASE + (slot) * CONN_INTERVAL_STEP)

static K_SEM_DEFINE(sem_connected, 0, 1);
/* The central drives the CS setup sequence per link; serialized, so single-count. */
static K_SEM_DEFINE(sem_acl_security, 0, 1);
static K_SEM_DEFINE(sem_remote_caps, 0, 1);
static K_SEM_DEFINE(sem_config_created, 0, 1);
static K_SEM_DEFINE(sem_cs_security, 0, 1);

static struct bt_conn *conns[CONFIG_BT_MAX_CONN];
static struct bt_conn *last_connected;
static atomic_t conn_count;

/* Per-link CS-Turn subscription, indexed by slot. */
static struct bt_gatt_subscribe_params sub_params[CONFIG_BT_MAX_CONN];
static bool link_ready[CONFIG_BT_MAX_CONN];

/* The peripheral-initiator's GATT layout is fixed (GATT + GAP + our CS-Turn service, all
 * from identical firmware), so the CS-Turn handles are deterministic: char value at 18,
 * CCC at 19 (verified by a discovery dump). We use them directly - the enumerate-and-match
 * GATT discovery is racy in this config. A production build should discover via bt_gatt_dm. */
#define CS_TURN_VAL_HANDLE 18
#define CS_TURN_CCC_HANDLE 19

static K_SEM_DEFINE(sem_turn_done, 0, 1);

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	ARG_UNUSED(conn);
	if (err == BT_SECURITY_ERR_SUCCESS) {
		LOG_INF("ACL security changed: level %d", level);
		k_sem_give(&sem_acl_security);
	} else {
		LOG_WRN("ACL security failed (err %d)", err);
	}
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected to %s (err 0x%02X)", addr, err);

	if (err) {
		return;
	}

	for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		if (conns[i] == NULL) {
			conns[i] = bt_conn_ref(conn);
			break;
		}
	}
	last_connected = conn;
	atomic_inc(&conn_count);
	dk_set_led_on(CON_STATUS_LED);
	k_sem_give(&sem_connected);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02X)", reason);

	for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		if (conns[i] == conn) {
			bt_conn_unref(conns[i]);
			conns[i] = NULL;
			link_ready[i] = false;
			break;
		}
	}
	atomic_dec(&conn_count);
	if (atomic_get(&conn_count) == 0) {
		dk_set_led_off(CON_STATUS_LED);
	}
	/* Round-robin skips this slot until it is re-established; no re-scan here (the setup
	 * phase owns scanning) to avoid racing the BLE stack mid-round-robin. */
}

static void remote_capabilities_cb(struct bt_conn *conn, uint8_t status,
				   struct bt_conn_le_cs_capabilities *params)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("CS capability exchange completed.");
		k_sem_give(&sem_remote_caps);
	} else {
		LOG_WRN("CS capability exchange failed. (HCI status 0x%02x)", status);
	}
}

static void config_create_cb(struct bt_conn *conn, uint8_t status,
			     struct bt_conn_le_cs_config *config)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("CS config creation complete (id %u, role %u, IPT).", config->id,
			config->role);
		k_sem_give(&sem_config_created);
	} else {
		LOG_WRN("CS config creation failed. (HCI status 0x%02x)", status);
	}
}

static void security_enable_cb(struct bt_conn *conn, uint8_t status)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("CS security enabled.");
		k_sem_give(&sem_cs_security);
	} else {
		LOG_WRN("CS security enable failed. (HCI status 0x%02x)", status);
	}
}

static void procedure_enable_cb(struct bt_conn *conn, uint8_t status,
				struct bt_conn_le_cs_procedure_enable_complete *params)
{
	ARG_UNUSED(conn);

	/* The peripheral-initiator enables procedures; the reflector just observes. */
	if (status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("CS procedures %s.", params->state == 1 ? "enabled" : "disabled");
	} else {
		LOG_WRN("CS procedures enable failed. (HCI status 0x%02x)", status);
	}
}

BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.security_changed = security_changed,
	.le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
	.le_cs_config_complete = config_create_cb,
	.le_cs_security_enable_complete = security_enable_cb,
	.le_cs_procedure_enable_complete = procedure_enable_cb,
};

/* DONE notification from an initiator: its granted turn finished. */
static uint8_t turn_notify_func(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
				const void *data, uint16_t length)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (data && length >= 1) {
		k_sem_give(&sem_turn_done);
	}
	return BT_GATT_ITER_CONTINUE;
}

/* Subscribe to a link's CS-Turn DONE notifications using the fixed handles. */
static int subscribe_turn(struct bt_conn *conn, int slot)
{
	sub_params[slot].notify = turn_notify_func;
	sub_params[slot].value = BT_GATT_CCC_NOTIFY;
	sub_params[slot].value_handle = CS_TURN_VAL_HANDLE;
	sub_params[slot].ccc_handle = CS_TURN_CCC_HANDLE;

	int err = bt_gatt_subscribe(conn, &sub_params[slot]);

	if (err && err != -EALREADY) {
		LOG_ERR("[slot %d] subscribe failed (err %d)", slot, err);
		return err;
	}

	k_sleep(K_MSEC(100)); /* let the CCC write land before the first GO grant */
	link_ready[slot] = true;
	LOG_INF("[slot %d] CS-Turn subscribed (handle %u).", slot, CS_TURN_VAL_HANDLE);
	return 0;
}

static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match, bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	(void)bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
	LOG_INF("Filters matched. Address: %s connectable: %d", addr, connectable);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	LOG_INF("Connecting failed, restarting scanning");
	(void)bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
}

static void scan_connecting(struct bt_scan_device_info *device_info, struct bt_conn *conn)
{
	LOG_INF("Connecting");
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL, scan_connecting_error, scan_connecting);

static int scan_init(void)
{
	int err;
	struct bt_scan_init_param scan_params = {
		.scan_param = NULL,
		.conn_param = BT_LE_CONN_PARAM(CONN_INTERVAL_BASE, CONN_INTERVAL_BASE, 0,
					       BT_GAP_MS_TO_CONN_TIMEOUT(8000)),
		.connect_if_match = 1,
	};

	bt_scan_init(&scan_params);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_NAME, INITIATOR_NAME);
	if (err) {
		LOG_ERR("Scanning filters cannot be set (err %d)", err);
		return err;
	}

	err = bt_scan_filter_enable(BT_SCAN_NAME_FILTER, false);
	if (err) {
		LOG_ERR("Filters cannot be turned on (err %d)", err);
		return err;
	}

	return 0;
}

/* Drive the full CS setup as the central-reflector for one freshly connected link. */
static int setup_link(struct bt_conn *conn, int slot)
{
	int err;

	const struct bt_le_cs_set_default_settings_param default_settings = {
		.enable_initiator_role = false,
		.enable_reflector_role = true,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	err = bt_le_cs_set_default_settings(conn, &default_settings);
	if (err) {
		LOG_ERR("[slot %d] default CS settings failed (err %d)", slot, err);
		return err;
	}

	/* Set this link's (uniform) connection interval. */
	uint16_t interval = CONN_INTERVAL_FOR_SLOT(slot);
	struct bt_le_conn_param param = {
		.interval_min = interval,
		.interval_max = interval,
		.latency = 0,
		.timeout = BT_GAP_MS_TO_CONN_TIMEOUT(8000),
	};
	err = bt_conn_le_param_update(conn, &param);
	if (err) {
		LOG_WRN("[slot %d] conn param update to %u failed (err %d)", slot, interval, err);
	} else {
		LOG_INF("[slot %d] interval -> %u (%u.%u ms)", slot, interval, (interval * 5) / 4,
			((interval * 5) % 4) * 25);
	}

	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_ERR("[slot %d] ACL security failed (err %d)", slot, err);
		return err;
	}
	if (k_sem_take(&sem_acl_security, K_SECONDS(5)) != 0) {
		LOG_ERR("[slot %d] ACL security timed out.", slot);
		return -ETIMEDOUT;
	}

	err = bt_le_cs_read_remote_supported_capabilities(conn);
	if (err) {
		LOG_ERR("[slot %d] CS capability exchange failed (err %d)", slot, err);
		return err;
	}
	if (k_sem_take(&sem_remote_caps, K_SECONDS(5)) != 0) {
		LOG_ERR("[slot %d] CS capability exchange timed out.", slot);
		return -ETIMEDOUT;
	}

	struct bt_le_cs_create_config_params config_params = {
		.id = 0,
		.mode = BT_CONN_LE_CS_MAIN_MODE_2_NO_SUB_MODE,
		.min_main_mode_steps = 2,
		.max_main_mode_steps = 5,
		.main_mode_repetition = 0,
		.mode_0_steps = 3,
		.role = BT_CONN_LE_CS_ROLE_REFLECTOR,
		.rtt_type = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY,
		.cs_sync_phy = BT_CONN_LE_CS_SYNC_1M_PHY,
		.channel_map_repetition = 1,
		.channel_selection_type = BT_CONN_LE_CS_CHSEL_TYPE_3B,
		.ch3c_shape = BT_CONN_LE_CS_CH3C_SHAPE_HAT,
		.ch3c_jump = 2,
		.cs_enhancements_1 = 1, /* IPT */
	};
	bt_le_cs_set_valid_chmap_bits(config_params.channel_map);

	err = bt_le_cs_create_config(conn, &config_params,
				     BT_LE_CS_CREATE_CONFIG_CONTEXT_LOCAL_AND_REMOTE);
	if (err) {
		LOG_ERR("[slot %d] create CS config failed (err %d)", slot, err);
		return err;
	}
	if (k_sem_take(&sem_config_created, K_SECONDS(5)) != 0) {
		LOG_ERR("[slot %d] CS config creation timed out.", slot);
		return -ETIMEDOUT;
	}

	err = bt_le_cs_security_enable(conn);
	if (err) {
		LOG_ERR("[slot %d] CS security failed (err %d)", slot, err);
		return err;
	}
	if (k_sem_take(&sem_cs_security, K_SECONDS(5)) != 0) {
		LOG_ERR("[slot %d] CS security timed out.", slot);
		return -ETIMEDOUT;
	}

	LOG_INF("[slot %d] CS setup complete (IPT, role Reflector). %ld link(s) up.", slot,
		(long)atomic_get(&conn_count));
	return 0;
}

int main(void)
{
	int err;

	LOG_INF("Starting Channel Sounding IPT Reflector - BLE central hub (MAX_CONN=%d)",
		CONFIG_BT_MAX_CONN);

	dk_leds_init();

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	err = scan_init();
	if (err) {
		return 0;
	}

	/* SETUP PHASE (single-threaded): connect + set up + subscribe each peripheral-initiator
	 * one at a time, with NO CS running yet, until MAX_CONN are up or no new peer appears for
	 * a few seconds. Doing all setup before any ranging avoids racing the BLE stack. */
	int established = 0;

	while (established < CONFIG_BT_MAX_CONN) {
		err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
		if (err && err != -EALREADY) {
			LOG_ERR("Scanning failed to start (err %d)", err);
			break;
		}

		if (k_sem_take(&sem_connected, K_MSEC(4000)) != 0) {
			if (established >= 1) {
				LOG_INF("No more peers; proceeding with %d link(s).",
					established);
				break;
			}
			continue; /* none yet - keep scanning */
		}

		struct bt_conn *c = last_connected;
		int slot = -1;

		for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
			if (conns[i] == c) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			continue;
		}

		if (setup_link(c, slot) == 0 && subscribe_turn(c, slot) == 0) {
			established++;
		} else {
			LOG_WRN("[slot %d] setup/subscribe failed.", slot);
		}
	}

	(void)bt_scan_stop();

	/* ROUND-ROBIN PHASE: grant one ready link a turn (write GO), wait for its DONE
	 * notification (bounded burst), then advance. Only one link ranges at a time, so the
	 * links never contend for the radio - the fix for the free-running starvation.
	 *
	 * AUTO-RECONNECT is folded into the same loop: an empty slot (dropped/reset anchor) is
	 * scanned + re-established instead of being granted a turn. Collision-free because no CS
	 * turn is active during the reconnect (single-threaded), and bounded so the live anchors
	 * still get their turns between reconnect attempts. */
	LOG_INF("Round-robin turn scheduler started (%d link(s)).", established);
	int slot = 0;

	while (true) {
		struct bt_conn *c = conns[slot];

		if (c && link_ready[slot]) {
			uint8_t go = 1;

			/* Drain any stale DONE so this turn waits for its OWN completion. */
			k_sem_reset(&sem_turn_done);

			err = bt_gatt_write_without_response(c, CS_TURN_VAL_HANDLE, &go, sizeof(go),
							     false);
			if (err) {
				LOG_WRN("[slot %d] GO write failed (err %d)", slot, err);
				k_sleep(K_MSEC(50));
			} else if (k_sem_take(&sem_turn_done, K_MSEC(2000)) != 0) {
				LOG_WRN("[slot %d] turn DONE timeout.", slot);
			}
		} else if (c == NULL) {
			/* Dropped slot - reconnect + re-establish it. */
			err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
			if (!err || err == -EALREADY) {
				if (k_sem_take(&sem_connected, K_MSEC(1500)) == 0) {
					struct bt_conn *nc = last_connected;
					int ns = -1;

					for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
						if (conns[i] == nc) {
							ns = i;
							break;
						}
					}
					(void)bt_scan_stop();
					if (ns >= 0) {
						LOG_INF("[slot %d] reconnected; re-establishing.", ns);
						if (setup_link(nc, ns) == 0) {
							(void)subscribe_turn(nc, ns);
						}
					}
				} else {
					(void)bt_scan_stop();
				}
			}
		} else {
			k_sleep(K_MSEC(50)); /* connected but not yet ready */
		}

		slot = (slot + 1) % CONFIG_BT_MAX_CONN;
	}

	return 0;
}
