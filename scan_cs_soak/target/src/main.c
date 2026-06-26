/*
 * scan_cs_soak - TARGET
 *
 * The peer the gateway ranges against. Advertises connectably as "CS Sample" and
 * re-advertises after each disconnect so the gateway can reconnect every cycle.
 *
 * CS role is selectable (compile-time), always the OPPOSITE of the gateway -
 * because CS role is independent of BLE role, and the gateway (BLE central) always
 * drives the CS setup either way:
 *   - default: CS reflector (gateway = CS initiator). The target just enables the
 *     reflector role; the gateway drives everything.
 *   - CONFIG_APP_CS_ROLE_INITIATOR=y: CS initiator (gateway = CS reflector). The
 *     gateway still drives caps/config/security; this target enables the CS
 *     procedures once CS security is up (the role-swap pattern).
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include "cs_ifft.h"

#define CS_CONFIG_ID         0
#define PROCEDURES_PER_CYCLE 1
#define PROCEDURE_INTERVAL   10

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_disconnected, 0, 1);

static struct bt_conn *connection;
static uint32_t conn_count;

static const char sample_str[] = "CS Sample";
static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, "CS Sample", sizeof(sample_str) - 1),
};

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("[tgt] connection failed (0x%02x)\n", err);
		return;
	}

	connection = bt_conn_ref(conn);
	conn_count++;
	printk("[tgt] connected (#%u)\n", conn_count);
	k_sem_give(&sem_connected);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	printk("[tgt] disconnected (0x%02x)\n", reason);

	if (connection) {
		bt_conn_unref(connection);
		connection = NULL;
	}
	k_sem_give(&sem_disconnected);
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(level);

	if (err) {
		printk("[tgt] encryption failed (%d)\n", err);
	}
}

#if defined(CONFIG_APP_CS_ROLE_INITIATOR)
/* CS initiator path: the gateway (central) drives setup; we enable the procedures.
 * Enabling CS must run in thread context, so the security-enable callback only sets
 * the procedure parameters and signals this thread to enable. */
static K_SEM_DEFINE(sem_enable_procedures, 0, 1);
static K_SEM_DEFINE(sem_ifft_compute, 0, 1);

static void cs_security_enable_cb(struct bt_conn *conn, uint8_t status)
{
	if (status != BT_HCI_ERR_SUCCESS) {
		printk("[tgt-init] CS security enable failed (0x%02x)\n", status);
		return;
	}

	static const struct bt_le_cs_set_procedure_parameters_param pp = {
		.config_id = CS_CONFIG_ID,
		.max_procedure_len = 0xffff,
		.min_procedure_interval = PROCEDURE_INTERVAL,
		.max_procedure_interval = PROCEDURE_INTERVAL,
		.max_procedure_count = PROCEDURES_PER_CYCLE,
		.min_subevent_len = 50000,
		.max_subevent_len = 50000,
		.tone_antenna_config_selection = BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
		.phy = BT_LE_CS_PROCEDURE_PHY_1M,
		.tx_power_delta = 0x80,
		.preferred_peer_antenna = BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1,
		.snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
		.snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
	};

	int err = bt_le_cs_set_procedure_parameters(conn, &pp);

	if (err) {
		printk("[tgt-init] set_proc_params (%d)\n", err);
		return;
	}

	k_sem_give(&sem_enable_procedures);
}

static void cs_procedure_enable_cb(struct bt_conn *conn, uint8_t status,
				   struct bt_conn_le_cs_procedure_enable_complete *params)
{
	ARG_UNUSED(conn);

	if (status != BT_HCI_ERR_SUCCESS) {
		printk("[tgt-init] procedure enable/disable failed (0x%02x)\n", status);
		return;
	}
	printk("[tgt-init] CS procedures %s\n", params->state ? "enabled" : "disabled");
}

static void cs_subevent_cb(struct bt_conn *conn, struct bt_conn_le_cs_subevent_result *result)
{
	ARG_UNUSED(conn);

	/* Accumulate tones (light); defer the heavy IFFT to the compute thread. */
	if (cs_ifft_accumulate(result)) {
		printk("[tgt-init] CS procedure %u complete (steps=%u)\n",
		       result->header.procedure_counter, result->header.num_steps_reported);
		k_sem_give(&sem_ifft_compute);
	}
}

static void enable_thread(void)
{
	while (1) {
		k_sem_take(&sem_enable_procedures, K_FOREVER);

		struct bt_conn *conn = connection;

		if (!conn) {
			continue;
		}

		struct bt_le_cs_procedure_enable_param en = {.config_id = CS_CONFIG_ID, .enable = 1};
		int err = bt_le_cs_procedure_enable(conn, &en);

		if (err) {
			printk("[tgt-init] procedure_enable (%d)\n", err);
		}
	}
}
K_THREAD_DEFINE(enable_tid, 2048, enable_thread, NULL, NULL, NULL, 7, 0, 0);

/* Compute the IFFT distance in thread context (cs_de_ifft is too heavy for the
 * subevent callback). */
static void ifft_thread(void)
{
	while (1) {
		k_sem_take(&sem_ifft_compute, K_FOREVER);
		cs_ifft_compute_print();
	}
}
K_THREAD_DEFINE(ifft_tid, 4096, ifft_thread, NULL, NULL, NULL, 7, 0, 0);
#endif /* CONFIG_APP_CS_ROLE_INITIATOR */

BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.security_changed = security_changed_cb,
#if defined(CONFIG_APP_CS_ROLE_INITIATOR)
	.le_cs_security_enable_complete = cs_security_enable_cb,
	.le_cs_procedure_enable_complete = cs_procedure_enable_cb,
	.le_cs_subevent_data_available = cs_subevent_cb,
#endif
};

int main(void)
{
	int err;

#if defined(CONFIG_APP_CS_ROLE_INITIATOR)
	const char *role_str = "initiator";
#else
	const char *role_str = "reflector";
#endif

	printk("\n*** scan_cs_soak TARGET (CS %s) - re-advertise loop ***\n", role_str);

	err = bt_enable(NULL);
	if (err) {
		printk("bt_enable (%d)\n", err);
		return 0;
	}

	while (1) {
		err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_1,
						      BT_GAP_ADV_FAST_INT_MAX_1, NULL),
				      ad, ARRAY_SIZE(ad), NULL, 0);
		if (err) {
			printk("[tgt] adv_start (%d)\n", err);
			k_sleep(K_MSEC(500));
			continue;
		}
		printk("[tgt] advertising as '%s'\n", sample_str);

		k_sem_take(&sem_connected, K_FOREVER);

		/* Set our CS role for this connection. The gateway (BLE central) drives
		 * caps exchange, config creation and security regardless; in initiator
		 * mode we additionally enable the procedures (see cs_security_enable_cb).
		 */
		const struct bt_le_cs_set_default_settings_param def = {
#if defined(CONFIG_APP_CS_ROLE_INITIATOR)
			.enable_initiator_role = true,
			.enable_reflector_role = false,
#else
			.enable_initiator_role = false,
			.enable_reflector_role = true,
#endif
			.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
			.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
		};

		err = bt_le_cs_set_default_settings(connection, &def);
		if (err) {
			printk("[tgt] set_default_settings (%d)\n", err);
		}

		k_sem_take(&sem_disconnected, K_FOREVER);
		/* Advertising auto-stopped on connect; the loop restarts it. */
	}

	return 0;
}
