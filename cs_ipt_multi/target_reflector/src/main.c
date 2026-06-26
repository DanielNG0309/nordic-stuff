/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Channel Sounding Reflector with Inline PCT Transfer sample
 */

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/cs.h>
#include <dk_buttons_and_leds.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

#define CON_STATUS_LED DK_LED1

/* "CS turn" GATT service: the reflector NOTIFIES one anchor "go" (value 1) so only
 * that anchor ranges, and the anchor WRITES "done" (value 0) when its burst finishes.
 * This serializes CS across anchors (round-robin) so concurrent free-running CS
 * procedures can't collide/starve each other. */
#define CS_TURN_SVC_UUID BT_UUID_128_ENCODE(0xca5e0001, 0x1234, 0x5678, 0x9abc, 0xdef012345678)
#define CS_TURN_CHR_UUID BT_UUID_128_ENCODE(0xca5e0002, 0x1234, 0x5678, 0x9abc, 0xdef012345678)

static struct bt_uuid_128 turn_svc_uuid = BT_UUID_INIT_128(CS_TURN_SVC_UUID);
static struct bt_uuid_128 turn_chr_uuid = BT_UUID_INIT_128(CS_TURN_CHR_UUID);

/* Number of currently connected anchors (initiators). */
static atomic_t conn_count;

/* Per-slot anchor connection refs + subscription state, for the round-robin. */
static struct bt_conn *anchors[CONFIG_BT_MAX_CONN];
static bool subscribed[CONFIG_BT_MAX_CONN];
static K_MUTEX_DEFINE(anchors_lock);

/* The anchor currently granted its turn, and a sem given when it reports "done". */
static struct bt_conn *active_turn_conn;
static K_SEM_DEFINE(sem_turn_done, 0, 1);

/* Anchor writes here when its CS burst is complete. */
static ssize_t turn_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(buf);
	ARG_UNUSED(offset);
	ARG_UNUSED(flags);

	if (conn == active_turn_conn) {
		k_sem_give(&sem_turn_done);
	}
	return len;
}

static void turn_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
	/* Subscription state is tracked per-connection in the round-robin via a notify
	 * attempt; nothing required here. */
}

BT_GATT_SERVICE_DEFINE(turn_svc,
	BT_GATT_PRIMARY_SERVICE(&turn_svc_uuid),
	BT_GATT_CHARACTERISTIC(&turn_chr_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, turn_write, NULL),
	BT_GATT_CCC(turn_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Reflector role settings applied per connection. The initiator drives the rest of
 * the CS setup (config, security, procedure-enable); the reflector just reflects. */
static const struct bt_le_cs_set_default_settings_param default_settings = {
	.enable_initiator_role = false,
	.enable_reflector_role = true,
	.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
	.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
};

/* New connections are handed to a worker thread. All HCI-synchronous work
 * (bt_le_cs_set_default_settings + advertising restart) runs there, never in the
 * connection callback context - blocking in the callback was causing peer
 * connections to "fail to establish" under multi-anchor contention. */
K_MSGQ_DEFINE(new_conn_q, sizeof(struct bt_conn *), CONFIG_BT_MAX_CONN, sizeof(void *));
static K_SEM_DEFINE(sem_refl_work, 0, 1);

/* (Re)start connectable advertising so additional anchors can attach. Advertising
 * auto-stops on each connection, so we restart it until we reach BT_MAX_CONN.
 * Called only from the worker thread (HCI-synchronous). */
static void advertising_start(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err && err != -EALREADY) {
		LOG_ERR("Advertising failed to start (err %d)", err);
	}
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_WRN("Connection failed to %s (err 0x%02X)", addr, err);
		k_sem_give(&sem_refl_work); /* let the worker re-advertise */
		return;
	}

	int count = atomic_inc(&conn_count) + 1;
	int idx = bt_conn_index(conn);

	LOG_INF("Connected to %s (anchor %d/%d, slot %d)", addr, count, CONFIG_BT_MAX_CONN, idx);
	dk_set_led_on(CON_STATUS_LED);

	/* Track the connection for the round-robin (its own ref, held until disconnect). */
	k_mutex_lock(&anchors_lock, K_FOREVER);
	anchors[idx] = bt_conn_ref(conn);
	subscribed[idx] = false;
	k_mutex_unlock(&anchors_lock);

	/* Hand the connection to the worker for CS setup; no HCI in this callback. */
	struct bt_conn *ref = bt_conn_ref(conn);

	if (k_msgq_put(&new_conn_q, &ref, K_NO_WAIT) != 0) {
		bt_conn_unref(ref);
	}
	k_sem_give(&sem_refl_work);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	int count = atomic_dec(&conn_count) - 1;
	int idx = bt_conn_index(conn);

	LOG_INF("Disconnected from %s (reason 0x%02X), %d anchors remaining", addr, reason, count);

	k_mutex_lock(&anchors_lock, K_FOREVER);
	if (anchors[idx]) {
		bt_conn_unref(anchors[idx]);
		anchors[idx] = NULL;
	}
	subscribed[idx] = false;
	k_mutex_unlock(&anchors_lock);

	if (count <= 0) {
		dk_set_led_off(CON_STATUS_LED);
	}

	/* No reboot (the stock sample rebooted here). Worker resumes advertising so the
	 * anchor can reconnect and ranging continues without a reset. */
	k_sem_give(&sem_refl_work);
}

/* Worker thread: performs all blocking HCI work outside the connection callbacks. */
static void reflector_worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct bt_conn *c;

		k_sem_take(&sem_refl_work, K_FOREVER);

		/* Configure CS reflector role for every newly connected anchor. */
		while (k_msgq_get(&new_conn_q, &c, K_NO_WAIT) == 0) {
			/* If this anchor reset and reconnected before its previous link
			 * supervision-timed-out (~4 s), the stale duplicate is still in anchors[]
			 * and its dead connection events disrupt the live anchors' CS for those
			 * seconds (the round-robin even wastes whole turns notifying it). The
			 * reconnecting anchor reuses its identity address, so drop any other slot
			 * holding the same peer address now. */
			const bt_addr_le_t *naddr = bt_conn_get_dst(c);
			int cidx = bt_conn_index(c);

			k_mutex_lock(&anchors_lock, K_FOREVER);
			for (int j = 0; j < CONFIG_BT_MAX_CONN; j++) {
				if (anchors[j] && j != cidx &&
				    bt_addr_le_eq(bt_conn_get_dst(anchors[j]), naddr)) {
					LOG_INF("Dropping stale duplicate of reconnected anchor (slot %d)",
						j);
					bt_conn_disconnect(anchors[j],
							   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
				}
			}
			k_mutex_unlock(&anchors_lock);

			int err = bt_le_cs_set_default_settings(c, &default_settings);

			if (err) {
				LOG_ERR("Failed to set reflector CS settings (err %d)", err);
			}
			bt_conn_unref(c);
		}

		/* Keep advertising while there is room for more anchors. */
		if (atomic_get(&conn_count) < CONFIG_BT_MAX_CONN) {
			advertising_start();
		}
	}
}

K_THREAD_DEFINE(refl_worker, 2048, reflector_worker, NULL, NULL, NULL, 5, 0, 0);

/* Round-robin coordinator: grant exactly one subscribed anchor a turn at a time so
 * only one CS procedure runs concurrently (no contention). Notify "go", wait for the
 * anchor's "done" write (or a timeout), then advance to the next anchor. */
static void round_robin_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint8_t go = 1;
	int idx = 0;

	while (true) {
		struct bt_conn *c = NULL;

		k_mutex_lock(&anchors_lock, K_FOREVER);
		for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
			int j = (idx + i) % CONFIG_BT_MAX_CONN;

			if (anchors[j] && bt_gatt_is_subscribed(anchors[j], &turn_svc.attrs[2],
								BT_GATT_CCC_NOTIFY)) {
				c = bt_conn_ref(anchors[j]);
				idx = j;
				break;
			}
		}
		k_mutex_unlock(&anchors_lock);

		if (!c) {
			/* No anchor ready yet. */
			k_msleep(50);
			idx = 0;
			continue;
		}

		active_turn_conn = c;
		k_sem_reset(&sem_turn_done);

		if (bt_gatt_notify(c, &turn_svc.attrs[2], &go, sizeof(go)) == 0) {
			/* Wait for the anchor's CS burst to finish, or time out and advance so
			 * one slow/lost anchor can't wedge the rotation. Must exceed the anchor's
			 * (≤950 ms) result-wait + compute + "done" write at the longest
			 * de-correlated interval. A successful turn releases this early, so the
			 * wider ceiling only bounds aborted turns. */
			(void)k_sem_take(&sem_turn_done, K_MSEC(1150));
		}

		active_turn_conn = NULL;
		bt_conn_unref(c);
		idx = (idx + 1) % CONFIG_BT_MAX_CONN;
		k_yield();
	}
}

K_THREAD_DEFINE(rr_thread, 2048, round_robin_thread, NULL, NULL, NULL, 6, 0, 0);

static void remote_capabilities_cb(struct bt_conn *conn, uint8_t status,
				   struct bt_conn_le_cs_capabilities *params)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("CS capability exchange completed.");
	} else {
		LOG_WRN("CS capability exchange failed. (HCI status 0x%02x)", status);
	}
}

static void config_create_cb(struct bt_conn *conn, uint8_t status,
			     struct bt_conn_le_cs_config *config)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		const char *mode_str[5] = {"Unused", "1 (RTT)", "2 (PBR)", "3 (RTT + PBR)",
					   "Invalid"};
		const char *role_str[3] = {"Initiator", "Reflector", "Invalid"};
		const char *rtt_type_str[8] = {
			"AA only",	 "32-bit sounding", "96-bit sounding", "32-bit random",
			"64-bit random", "96-bit random",   "128-bit random",  "Invalid"};
		const char *phy_str[4] = {"Invalid", "LE 1M PHY", "LE 2M PHY", "LE 2M 2BT PHY"};
		const char *chsel_type_str[3] = {"Algorithm #3b", "Algorithm #3c", "Invalid"};
		const char *ch3c_shape_str[3] = {"Hat shape", "X shape", "Invalid"};

		uint8_t mode_idx = config->mode > 0 && config->mode < 4 ? config->mode : 4;
		uint8_t role_idx = MIN(config->role, 2);
		uint8_t rtt_type_idx = MIN(config->rtt_type, 7);
		uint8_t phy_idx = config->cs_sync_phy > 0 && config->cs_sync_phy < 4
					  ? config->cs_sync_phy
					  : 0;
		uint8_t chsel_type_idx = MIN(config->channel_selection_type, 2);
		uint8_t ch3c_shape_idx = MIN(config->ch3c_shape, 2);

		LOG_INF("CS config creation complete.\n"
			" - id: %u\n"
			" - mode: %s\n"
			" - min_main_mode_steps: %u\n"
			" - max_main_mode_steps: %u\n"
			" - main_mode_repetition: %u\n"
			" - mode_0_steps: %u\n"
			" - role: %s\n"
			" - rtt_type: %s\n"
			" - cs_sync_phy: %s\n"
			" - channel_map_repetition: %u\n"
			" - channel_selection_type: %s\n"
			" - ch3c_shape: %s\n"
			" - ch3c_jump: %u\n"
			" - t_ip1_time_us: %u\n"
			" - t_ip2_time_us: %u\n"
			" - t_fcs_time_us: %u\n"
			" - t_pm_time_us: %u\n"
			" - channel_map: 0x%08X%08X%04X\n",
			config->id, mode_str[mode_idx], config->min_main_mode_steps,
			config->max_main_mode_steps, config->main_mode_repetition,
			config->mode_0_steps, role_str[role_idx], rtt_type_str[rtt_type_idx],
			phy_str[phy_idx], config->channel_map_repetition,
			chsel_type_str[chsel_type_idx], ch3c_shape_str[ch3c_shape_idx],
			config->ch3c_jump, config->t_ip1_time_us, config->t_ip2_time_us,
			config->t_fcs_time_us, config->t_pm_time_us,
			sys_get_le32(&config->channel_map[6]),
			sys_get_le32(&config->channel_map[2]),
			sys_get_le16(&config->channel_map[0]));

	} else {
		LOG_WRN("CS config creation failed. (HCI status 0x%02x)", status);
	}
}

static void security_enable_cb(struct bt_conn *conn, uint8_t status)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		LOG_INF("CS security enabled.");
	} else {
		LOG_WRN("CS security enable failed. (HCI status 0x%02x)", status);
	}
}

static void procedure_enable_cb(struct bt_conn *conn, uint8_t status,
				struct bt_conn_le_cs_procedure_enable_complete *params)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		if (params->state == 1) {
			LOG_INF("CS procedures enabled:\n"
				" - config ID: %u\n"
				" - antenna configuration index: %u\n"
				" - TX power: %d dbm\n"
				" - subevent length: %u us\n"
				" - subevents per event: %u\n"
				" - subevent interval: %u\n"
				" - event interval: %u\n"
				" - procedure interval: %u\n"
				" - procedure count: %u\n"
				" - maximum procedure length: %u",
				params->config_id, params->tone_antenna_config_selection,
				params->selected_tx_power, params->subevent_len,
				params->subevents_per_event, params->subevent_interval,
				params->event_interval, params->procedure_interval,
				params->procedure_count, params->max_procedure_len);
		} else {
			LOG_INF("CS procedures disabled.");
		}
	} else {
		LOG_WRN("CS procedures enable failed. (HCI status 0x%02x)", status);
	}
}

BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
	.le_cs_config_complete = config_create_cb,
	.le_cs_security_enable_complete = security_enable_cb,
	.le_cs_procedure_enable_complete = procedure_enable_cb,
};

int main(void)
{
	int err;

	LOG_INF("Starting Channel Sounding IPT Reflector Sample");

	dk_leds_init();

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	/* Kick the worker to start advertising (keeps all HCI off the main/callback path).
	 * Per-connection CS setup also happens in the worker; the controller time-slices
	 * CS across all connected anchors. */
	k_sem_give(&sem_refl_work);

	return 0;
}
