/*
 * scan_cs_soak — GATEWAY_MODESWITCH
 *
 * The Observer <-> CS *mode switch* architecture (the one a scanning gateway might
 * naively attempt), implemented correctly:
 *   - scan as an Observer,
 *   - on a periodic "server command" trigger: STOP the scanner, connect, run a
 *     Channel Sounding procedure, disable + bt_le_cs_remove_config, disconnect,
 *     and then RESTART the scanner.
 *
 * The difference vs the continuous `gateway`: there is NO
 * CONFIG_BT_SCAN_AND_INITIATE_IN_PARALLEL — the scanner is genuinely stopped
 * while CS runs and restarted afterwards. The restart is the classic failure
 * point for this pattern; here it recovers cleanly every cycle (no reboot).
 *
 * Scanner-health metric: scan_total must keep climbing across cycles, proving
 * the scanner restarts successfully each time. (It only counts while scanning,
 * so the per-window rate dips during each CS op and recovers after.)
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/debug/thread_analyzer.h>
#include "cs_ifft.h"

#define CS_CONFIG_ID         0
#define NUM_MODE_0_STEPS     1
#define PROCEDURES_PER_CYCLE 1
#define PROCEDURE_INTERVAL   10 /* ACL connection events between CS procedures */
#define CS_PERIOD_MS         3000 /* run one CS op this often; scan in between */
#define TARGET_NAME          "CS Sample"
#define NAME_LEN             30

/* CS role is independent of the BLE role: the gateway is always the BLE central
 * and always drives CS setup, but it can be EITHER CS role. Default = initiator;
 * CONFIG_APP_CS_ROLE_REFLECTOR=y makes it the reflector (peer becomes initiator). */
#if defined(CONFIG_APP_CS_ROLE_REFLECTOR)
#define APP_CS_ROLE     BT_CONN_LE_CS_ROLE_REFLECTOR
#define APP_ENABLE_INIT false
#define APP_ENABLE_REFL true
#define APP_ROLE_STR    "reflector"
#else
#define APP_CS_ROLE     BT_CONN_LE_CS_ROLE_INITIATOR
#define APP_ENABLE_INIT true
#define APP_ENABLE_REFL false
#define APP_ROLE_STR    "initiator"
#endif

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_disconnected, 0, 1);
static K_SEM_DEFINE(sem_acl_encrypted, 0, 1);
static K_SEM_DEFINE(sem_remote_caps, 0, 1);
static K_SEM_DEFINE(sem_config_created, 0, 1);
static K_SEM_DEFINE(sem_cs_security, 0, 1);
static K_SEM_DEFINE(sem_procedure_done, 0, 1);
static K_SEM_DEFINE(sem_procedure_disabled, 0, 1);

static struct bt_conn *connection;
static atomic_t connecting = ATOMIC_INIT(0);
static atomic_t want_cs = ATOMIC_INIT(0); /* set by the periodic trigger */

/* Stats */
static atomic_t scan_reports = ATOMIC_INIT(0);
static atomic_t scan_window = ATOMIC_INIT(0);
static uint32_t cycle_count;
static uint32_t cs_proc_total;
static uint32_t scan_restarts;
static uint16_t last_proc_counter;
static uint8_t last_num_steps;

static int resume_scan(void);

/* ---------------- CS / connection callbacks ---------------- */

static void subevent_result_cb(struct bt_conn *conn, struct bt_conn_le_cs_subevent_result *result)
{
	last_proc_counter = result->header.procedure_counter;
	last_num_steps = result->header.num_steps_reported;

#if !defined(CONFIG_APP_CS_ROLE_REFLECTOR)
	/* As the CS initiator, accumulate this subevent's tones (light). The heavy
	 * IFFT runs in thread context (run_cs_cycle) after the procedure completes. */
	(void)cs_ifft_accumulate(result);
#endif

	if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_COMPLETE) {
		k_sem_give(&sem_procedure_done);
	}
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("[ms] connect to %s failed (0x%02x)\n", addr, err);
		if (connection) {
			bt_conn_unref(connection);
			connection = NULL;
		}
		atomic_set(&connecting, 0);
		atomic_set(&want_cs, 0);
		/* Connect failed -> the scanner is stopped; bring it back. */
		(void)resume_scan();
		return;
	}

	printk("[ms] connected to %s\n", addr);
	k_sem_give(&sem_connected);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	printk("[ms] disconnected (0x%02x)\n", reason);

	if (connection) {
		bt_conn_unref(connection);
		connection = NULL;
	}
	k_sem_give(&sem_disconnected);
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	if (err) {
		printk("[ms] encryption failed (%d)\n", err);
	}
	k_sem_give(&sem_acl_encrypted);
}

static void remote_capabilities_cb(struct bt_conn *conn, uint8_t status,
				   struct bt_conn_le_cs_capabilities *params)
{
	ARG_UNUSED(params);

	if (status == BT_HCI_ERR_SUCCESS) {
		k_sem_give(&sem_remote_caps);
	} else {
		printk("[ms] CS caps exchange failed (0x%02x)\n", status);
	}
}

static void config_create_cb(struct bt_conn *conn, uint8_t status,
			     struct bt_conn_le_cs_config *config)
{
	if (status == BT_HCI_ERR_SUCCESS) {
		k_sem_give(&sem_config_created);
	} else {
		printk("[ms] CS config create failed (0x%02x)\n", status);
	}
}

static void security_enable_cb(struct bt_conn *conn, uint8_t status)
{
	if (status == BT_HCI_ERR_SUCCESS) {
		k_sem_give(&sem_cs_security);
	} else {
		printk("[ms] CS security enable failed (0x%02x)\n", status);
	}
}

static void procedure_enable_cb(struct bt_conn *conn, uint8_t status,
				struct bt_conn_le_cs_procedure_enable_complete *params)
{
	if (status != BT_HCI_ERR_SUCCESS) {
		printk("[ms] CS procedure enable/disable failed (0x%02x)\n", status);
		return;
	}

	if (params->state == 0) {
		k_sem_give(&sem_procedure_disabled);
	}
}

BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.security_changed = security_changed_cb,
	.le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
	.le_cs_config_complete = config_create_cb,
	.le_cs_security_enable_complete = security_enable_cb,
	.le_cs_procedure_enable_complete = procedure_enable_cb,
	.le_cs_subevent_data_available = subevent_result_cb,
};

/* ---------------- scanning (stopped during CS, restarted after) ---------------- */

static bool name_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;
	uint8_t len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, NAME_LEN - 1);
		memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
	default:
		return true;
	}
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char name[NAME_LEN] = {};
	int err;

	atomic_inc(&scan_reports);
	atomic_inc(&scan_window);

	/* Only act when a CS op has been triggered, and only one at a time. */
	if (!atomic_get(&want_cs) || connection || atomic_get(&connecting)) {
		return;
	}

	if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	bt_data_parse(ad, name_cb, name);
	if (strcmp(name, TARGET_NAME)) {
		return;
	}

	if (!atomic_cas(&connecting, 0, 1)) {
		return;
	}

	/* MODE SWITCH: stop scanning before connecting. */
	err = bt_le_scan_stop();
	if (err) {
		printk("[ms] scan_stop (%d)\n", err);
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &connection);
	if (err) {
		printk("[ms] bt_conn_le_create failed (%d)\n", err);
		atomic_set(&connecting, 0);
		atomic_set(&want_cs, 0);
		(void)resume_scan();
	}
}

static int resume_scan(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CONTINUOUS, device_found);

	if (err) {
		/* The classic failure point for this pattern — must NOT happen. */
		printk("[ms] !!! scan RESTART FAILED (%d) !!!\n", err);
	} else {
		scan_restarts++;
	}
	return err;
}

static void cs_trigger(struct k_timer *t)
{
	ARG_UNUSED(t);
	atomic_set(&want_cs, 1);
}
K_TIMER_DEFINE(cs_trigger_timer, cs_trigger, NULL);

static struct bt_le_cs_create_config_params get_cs_config_params(void)
{
	struct bt_le_cs_create_config_params p = {
		.id = CS_CONFIG_ID,
		.mode = BT_CONN_LE_CS_MAIN_MODE_2_SUB_MODE_1,
		.min_main_mode_steps = 2,
		.max_main_mode_steps = 10,
		.main_mode_repetition = 0,
		.mode_0_steps = NUM_MODE_0_STEPS,
		.role = APP_CS_ROLE,
		.rtt_type = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY,
		.cs_sync_phy = BT_CONN_LE_CS_SYNC_1M_PHY,
		.channel_map_repetition = 1,
		.channel_selection_type = BT_CONN_LE_CS_CHSEL_TYPE_3B,
		.ch3c_shape = BT_CONN_LE_CS_CH3C_SHAPE_HAT,
		.ch3c_jump = 2,
		/* Inline PCT Transfer: the CS initiator gets the reflector's phase data
		 * inline and computes distance locally (no RAS/GATT transfer). */
		.cs_enhancements_1 = 1,
	};

	memset(p.channel_map, 0, 10);
	for (uint8_t i = 26; i < 62; i++) {
		BT_LE_CS_CHANNEL_BIT_SET_VAL(p.channel_map, i, 1);
	}

	return p;
}

static int run_cs_cycle(struct bt_conn *conn)
{
	int err;
	int done = 0;

	const struct bt_le_cs_set_default_settings_param def = {
		.enable_initiator_role = APP_ENABLE_INIT,
		.enable_reflector_role = APP_ENABLE_REFL,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	err = bt_le_cs_set_default_settings(conn, &def);
	if (err) {
		printk("[ms] set_default_settings (%d)\n", err);
		return err;
	}

	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		printk("[ms] set_security (%d)\n", err);
		return err;
	}
	if (k_sem_take(&sem_acl_encrypted, K_SECONDS(5))) {
		printk("[ms] encryption timeout\n");
		return -ETIMEDOUT;
	}

	err = bt_le_cs_read_remote_supported_capabilities(conn);
	if (err) {
		printk("[ms] read_remote_caps (%d)\n", err);
		return err;
	}
	if (k_sem_take(&sem_remote_caps, K_SECONDS(5))) {
		printk("[ms] caps timeout\n");
		return -ETIMEDOUT;
	}

	struct bt_le_cs_create_config_params cfg = get_cs_config_params();

	err = bt_le_cs_create_config(conn, &cfg, BT_LE_CS_CREATE_CONFIG_CONTEXT_LOCAL_AND_REMOTE);
	if (err) {
		printk("[ms] create_config (%d)\n", err);
		return err;
	}
	if (k_sem_take(&sem_config_created, K_SECONDS(5))) {
		printk("[ms] config timeout\n");
		return -ETIMEDOUT;
	}

	err = bt_le_cs_security_enable(conn);
	if (err) {
		printk("[ms] cs_security_enable (%d)\n", err);
		return err;
	}
	if (k_sem_take(&sem_cs_security, K_SECONDS(5))) {
		printk("[ms] cs security timeout\n");
		return -ETIMEDOUT;
	}

#if defined(CONFIG_APP_CS_ROLE_REFLECTOR)
	/* CS reflector: the central drove the setup above, but the PEER (now the CS
	 * initiator) sets procedure parameters and enables the procedures. We just
	 * wait for our own reflector subevent results, then tear down.
	 */
	for (int i = 0; i < PROCEDURES_PER_CYCLE; i++) {
		if (k_sem_take(&sem_procedure_done, K_SECONDS(5))) {
			printk("[ms] (reflector) initiator procedure %d/%d timeout\n", i,
			       PROCEDURES_PER_CYCLE);
			break;
		}
		done++;
		cs_proc_total++;
	}
#else
	const struct bt_le_cs_set_procedure_parameters_param pp = {
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

	err = bt_le_cs_set_procedure_parameters(conn, &pp);
	if (err) {
		printk("[ms] set_proc_params (%d)\n", err);
		return err;
	}

	struct bt_le_cs_procedure_enable_param en = {.config_id = CS_CONFIG_ID, .enable = 1};

	err = bt_le_cs_procedure_enable(conn, &en);
	if (err) {
		printk("[ms] proc_enable (%d)\n", err);
		return err;
	}

	for (int i = 0; i < PROCEDURES_PER_CYCLE; i++) {
		if (k_sem_take(&sem_procedure_done, K_SECONDS(3))) {
			printk("[ms] procedure %d/%d timeout\n", i, PROCEDURES_PER_CYCLE);
			break;
		}
		done++;
		cs_proc_total++;
		/* Heavy IFFT in thread context (not the BT RX callback). */
		cs_ifft_compute_print();
	}

	/* Explicit teardown: disable procedures, wait for the disable to complete,
	 * THEN remove the config (removing before the disable lands returns -13).
	 */
	en.enable = 0;
	(void)bt_le_cs_procedure_enable(conn, &en);
	(void)k_sem_take(&sem_procedure_disabled, K_SECONDS(2));
#endif

	err = bt_le_cs_remove_config(conn, CS_CONFIG_ID);
	if (err) {
		printk("[ms] remove_config (%d)\n", err);
	} else {
		printk("[ms] remove_config ok\n");
	}

	return done > 0 ? 0 : -EIO;
}

static void stats_thread(void)
{
	while (1) {
		k_sleep(K_SECONDS(5));

		uint32_t w = (uint32_t)atomic_set(&scan_window, 0);

		printk("\n==== [ms] STATS  cycles=%u  cs_procs=%u  scan_restarts=%u  "
		       "last_proc=%u(steps=%u)  scan_total=%ld  scan/5s=%u (%.1f/s) ====\n",
		       cycle_count, cs_proc_total, scan_restarts, last_proc_counter,
		       last_num_steps, (long)atomic_get(&scan_reports), w, (double)w / 5.0);
		thread_analyzer_print(0);
	}
}
K_THREAD_DEFINE(stats_tid, 2048, stats_thread, NULL, NULL, NULL, 7, 0, 1000);

int main(void)
{
	int err;

	printk("\n*** scan_cs_soak GATEWAY_MODESWITCH (CS %s) — stop-scan -> CS -> restart-scan loop ***\n",
	       APP_ROLE_STR);

	err = bt_enable(NULL);
	if (err) {
		printk("bt_enable (%d)\n", err);
		return 0;
	}

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CONTINUOUS, device_found);
	if (err) {
		printk("scan_start (%d)\n", err);
		return 0;
	}
	printk("[ms] scanner started; CS op every %d ms\n", CS_PERIOD_MS);

	k_timer_start(&cs_trigger_timer, K_MSEC(CS_PERIOD_MS), K_MSEC(CS_PERIOD_MS));

	while (1) {
		/* device_found() stops the scanner and connects when triggered. */
		k_sem_take(&sem_connected, K_FOREVER);

		err = run_cs_cycle(connection);
		printk("[ms] cycle %u CS %s (%u procedures total)\n", cycle_count,
		       err ? "FAILED" : "ok", cs_proc_total);

		if (connection) {
			bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
		(void)k_sem_take(&sem_disconnected, K_SECONDS(5));

		cycle_count++;
		atomic_set(&connecting, 0);
		atomic_set(&want_cs, 0);

		/* MODE SWITCH: bring the scanner back. This is the step that wedged
		 * the classic failure point; here it must succeed every cycle.
		 */
		(void)resume_scan();
	}

	return 0;
}
