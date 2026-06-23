/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Channel Sounding Initiator with Inline PCT Transfer sample
 */

#include <math.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/scan.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/cs_de.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

#define CON_STATUS_LED	 DK_LED1
#define CS_CONFIG_ID	 0
#define NUM_MODE_0_STEPS 3
#define REFLECTOR_NAME	 "Nordic CS IPT Reflector"

#define CHANNEL_INDEX_OFFSET   (2)
#define MEDIAN_FILTER_SIZE (9)

static K_SEM_DEFINE(sem_remote_capabilities_obtained, 0, 1);
static K_SEM_DEFINE(sem_config_created, 0, 1);
static K_SEM_DEFINE(sem_cs_security_enabled, 0, 1);
static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_security, 0, 1);
static K_SEM_DEFINE(sem_subevent_results_parsed, 0, 1);
static K_SEM_DEFINE(sem_distance_estimate_updated, 1, 1);

/* CS-turn coordination (round-robin): this anchor only ranges when the reflector
 * notifies "go", and writes "done" when its CS burst finishes. Serializes CS across
 * anchors so concurrent procedures don't collide. Must match the reflector's UUIDs. */
#define CS_TURN_SVC_UUID BT_UUID_128_ENCODE(0xca5e0001, 0x1234, 0x5678, 0x9abc, 0xdef012345678)
#define CS_TURN_CHR_UUID BT_UUID_128_ENCODE(0xca5e0002, 0x1234, 0x5678, 0x9abc, 0xdef012345678)
static struct bt_uuid_128 turn_svc_uuid = BT_UUID_INIT_128(CS_TURN_SVC_UUID);
static struct bt_uuid_128 turn_chr_uuid = BT_UUID_INIT_128(CS_TURN_CHR_UUID);

static K_SEM_DEFINE(sem_turn, 0, 1);       /* given on each "go" notification */
static K_SEM_DEFINE(sem_discovered, 0, 1); /* given when turn-service subscribe done */
static K_SEM_DEFINE(sem_procedure_disabled, 0, 1); /* given when a count=1 burst fully stops */
static K_SEM_DEFINE(sem_disconnected, 0, 1); /* given on disconnect, for the reconnect loop */
static volatile bool link_up;              /* true between connected and disconnected */
static struct bt_gatt_subscribe_params turn_sub;
static uint16_t turn_value_handle;

static struct bt_conn *connection;
static struct bt_conn_le_cs_config cs_config;

/* "go" notification from the reflector: it's this anchor's turn to range. */
static uint8_t turn_notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			      const void *data, uint16_t length)
{
	ARG_UNUSED(conn);

	if (!data) {
		params->value_handle = 0;
		return BT_GATT_ITER_STOP;
	}
	k_sem_give(&sem_turn);
	return BT_GATT_ITER_CONTINUE;
}

static void turn_dm_completed(struct bt_gatt_dm *dm, void *ctx)
{
	ARG_UNUSED(ctx);
	struct bt_conn *conn = bt_gatt_dm_conn_get(dm);
	const struct bt_gatt_dm_attr *chrc = bt_gatt_dm_char_by_uuid(dm, &turn_chr_uuid.uuid);
	const struct bt_gatt_dm_attr *ccc;

	if (!chrc) {
		LOG_ERR("CS-turn characteristic not found");
		bt_gatt_dm_data_release(dm);
		return;
	}
	ccc = bt_gatt_dm_desc_by_uuid(dm, chrc, BT_UUID_GATT_CCC);
	if (!ccc) {
		LOG_ERR("CS-turn CCC not found");
		bt_gatt_dm_data_release(dm);
		return;
	}

	turn_value_handle = chrc->handle + 1;
	turn_sub.ccc_handle = ccc->handle;
	turn_sub.value_handle = turn_value_handle;
	turn_sub.value = BT_GATT_CCC_NOTIFY;
	turn_sub.notify = turn_notify_cb;

	int err = bt_gatt_subscribe(conn, &turn_sub);

	if (err && err != -EALREADY) {
		LOG_ERR("CS-turn subscribe failed (err %d)", err);
	}
	bt_gatt_dm_data_release(dm);
	k_sem_give(&sem_discovered);
}

static void turn_dm_not_found(struct bt_conn *conn, void *ctx)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(ctx);
	LOG_ERR("CS-turn service not found");
}

static void turn_dm_error(struct bt_conn *conn, int err, void *ctx)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(ctx);
	LOG_ERR("CS-turn discovery error (err %d)", err);
}

static struct bt_gatt_dm_cb turn_dm_cb = {
	.completed = turn_dm_completed,
	.service_not_found = turn_dm_not_found,
	.error_found = turn_dm_error,
};

/* Store local initiator IQs in this union.
 * The size is based on requirements of cs_de_ifft.
 * The scratch memory is used for the ifft operation.
 */
static union {
	struct {
		float i;
		float q;
	} values[CS_DE_NUM_CHANNELS];
	float scratch_mem[2 * CONFIG_BT_CS_DE_NFFT_SIZE];
} iq;

/* --- Sliding-window median filter for distance tracking ---
 *
 * Keeps the most recent MEDIAN_FILTER_SIZE distance measurements in a
 * circular buffer and reports the median of the valid (finite) samples.
 */
struct distance_estimate_buffer {
	float estimates[MEDIAN_FILTER_SIZE];
	uint8_t num_valid;
	uint8_t index;
};

static struct distance_estimate_buffer distance_estimate_buffer;

static void store_distance_estimate(float distance)
{
	distance_estimate_buffer.estimates[distance_estimate_buffer.index] = distance;
	distance_estimate_buffer.index =
		(distance_estimate_buffer.index + 1) % MEDIAN_FILTER_SIZE;

	if (distance_estimate_buffer.num_valid < MEDIAN_FILTER_SIZE) {
		distance_estimate_buffer.num_valid++;
	}
}

static int float_cmp(const void *a, const void *b)
{
	float fa = *(const float *)a;
	float fb = *(const float *)b;

	return (fa > fb) - (fa < fb);
}

static float median_inplace(int count, float *values)
{
	if (count == 0) {
		return NAN;
	}

	qsort(values, count, sizeof(float), float_cmp);

	if (count % 2 == 0) {
		return (values[count / 2] + values[count / 2 - 1]) / 2;
	} else {
		return values[count / 2];
	}
}

static float get_filtered_distance(void)
{
	static float temp_ifft[MEDIAN_FILTER_SIZE];
	uint8_t num_ifft = 0;

	for (uint8_t i = 0; i < distance_estimate_buffer.num_valid; i++) {
		if (isfinite(distance_estimate_buffer.estimates[i])) {
			temp_ifft[num_ifft] = distance_estimate_buffer.estimates[i];
			num_ifft++;
		}
	}

	return median_inplace(num_ifft, temp_ifft);
}

/* Read the connection RSSI for the DIST: output line (best effort). */
static int8_t read_conn_rssi(struct bt_conn *c)
{
	struct net_buf *buf, *rsp = NULL;
	struct bt_hci_cp_read_rssi *cp;
	struct bt_hci_rp_read_rssi *rp;
	uint16_t handle;
	int8_t rssi = 0;

	if (!c || bt_hci_get_conn_handle(c, &handle)) {
		return 0;
	}

	buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(*cp));
	if (!buf) {
		return 0;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);

	if (bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp)) {
		return 0;
	}

	rp = (void *)rsp->data;
	rssi = rp->rssi;
	net_buf_unref(rsp);

	return rssi;
}

/* Path-loss RSSI->distance fallback, carried in the DIST: line for the
 * multilateration tool to weight against. */
static float rssi_to_distance(int8_t rssi)
{
	const float reference_rssi = -60.0f; /* RSSI at 1 m */
	const float n = 2.2f;                /* indoor short-range path-loss exponent */
	float d;

	if (rssi > -30) {
		rssi = -30;
	}
	if (rssi < -90) {
		rssi = -90;
	}

	d = powf(10.0f, (reference_rssi - rssi) / (10.0f * n));
	return CLAMP(d, 0.1f, 10.0f);
}

static void distance_estimates_update(void)
{
	uint8_t samples = 0;

	for (uint8_t i = 0; i < CS_DE_NUM_CHANNELS; i++) {
		LOG_DBG("iq[%d]: %.1f + j * %.1f", i, (double)iq.values[i].i,
			(double)iq.values[i].q);
		/* Count channels that carry a tone. Must be done BEFORE cs_de_ifft(),
		 * which overwrites iq.values via the scratch_mem union. */
		if (iq.values[i].i != 0.0f || iq.values[i].q != 0.0f) {
			samples++;
		}
	}

	float distance_ifft = cs_de_ifft(iq.scratch_mem);

	if (isfinite(distance_ifft)) {
		static int64_t prev_ts = -1;

		int64_t now = k_uptime_get();
		int64_t delta = 0;

		if (prev_ts != -1) {
			delta = now - prev_ts;
		}
		prev_ts = now;

		store_distance_estimate(distance_ifft);

		float distance_median = get_filtered_distance();

		LOG_INF("Distance estimates: median: %.2fm, update: %.2fm, time_delta: %lldms",
			(double)distance_median, (double)distance_ifft, delta);

		/* Compatibility line for the Multilateration Python tool, which parses:
		 * DIST:%.3f,AP:%d,SAMPLES:%d,RSSI:%d,RSSI_DIST:%.3f  (IFFT median as DIST). */
		int8_t rssi = read_conn_rssi(connection);

		printk("DIST:%.3f,AP:0,SAMPLES:%d,RSSI:%d,RSSI_DIST:%.3f\n",
		       (double)distance_median, samples, rssi, (double)rssi_to_distance(rssi));
	}
	k_sem_give(&sem_distance_estimate_updated);
}

static void pcts_parse(uint8_t channel_index,
		       struct bt_hci_le_cs_step_data_tone_info *local_tone_info)
{
	struct bt_le_cs_iq_sample local_iq =
		bt_le_cs_parse_pct(local_tone_info[0].phase_correction_term);

	iq.values[channel_index - CHANNEL_INDEX_OFFSET].i = local_iq.i;
	iq.values[channel_index - CHANNEL_INDEX_OFFSET].q = local_iq.q;
}

static void subevent_steps_parse(struct bt_conn_le_cs_subevent_result *result)
{
	for (uint8_t i = 0; i < result->header.num_steps_reported; i++) {
		if (result->step_data_buf->len < 3) {
			LOG_WRN("Local step data appears malformed.");
			return;
		}
		struct bt_le_cs_subevent_step local_step = {0};

		local_step.mode = net_buf_simple_pull_u8(result->step_data_buf);
		local_step.channel = net_buf_simple_pull_u8(result->step_data_buf);
		local_step.data_len = net_buf_simple_pull_u8(result->step_data_buf);

		if (local_step.data_len == 0) {
			LOG_WRN("Encountered zero-length step data.");
			return;
		}
		if (local_step.data_len > result->step_data_buf->len) {
			LOG_WRN("Local step data appears malformed.");
			return;
		}

		local_step.data = result->step_data_buf->data;

		if (local_step.mode == BT_HCI_OP_LE_CS_MAIN_MODE_2) {
			struct bt_hci_le_cs_step_data_mode_2 *local_step_data =
				(struct bt_hci_le_cs_step_data_mode_2 *)local_step.data;
			pcts_parse(local_step.channel, local_step_data->tone_info);
		}

		net_buf_simple_pull(result->step_data_buf, local_step.data_len);
	}
}

static void subevent_result_cb(struct bt_conn *conn, struct bt_conn_le_cs_subevent_result *result)
{
	static int64_t prev_ts = -1;
	static uint32_t prev_procedure_counter = UINT16_MAX + 1;

	int64_t now = k_uptime_get();
	int64_t delta = 0;

	if (prev_ts != -1) {
		delta = now - prev_ts;
	}
	prev_ts = now;

	LOG_DBG("Subevent result callback for procedure counter %u procedure done status "
		"%u subevent done status %u, number of steps reported %u, time delta since "
		"last call: %lld ms",
		result->header.procedure_counter, result->header.procedure_done_status,
		result->header.subevent_done_status, result->header.num_steps_reported, delta);

	const bool cs_aborted = (result->header.procedure_abort_reason !=
				 BT_HCI_LE_CS_PROCEDURE_ABORT_REASON_NO_ABORT) ||
				(result->header.subevent_abort_reason !=
				 BT_HCI_LE_CS_SUBEVENT_ABORT_REASON_NO_ABORT);

	if (cs_aborted) {
		LOG_DBG("CS aborted. pc=%u proc_abort=%u subevent_abort=%u",
			result->header.procedure_counter, result->header.procedure_abort_reason,
			result->header.subevent_abort_reason);
		return;
	}

	if (result->header.procedure_counter != prev_procedure_counter) {
		/* NEVER block the BT RX thread here. The stock sample uses K_FOREVER because
		 * distance_estimates_update() (which gives this sem) runs every procedure. In
		 * our round-robin the ranging loop has a timeout and may skip that update, so a
		 * K_FOREVER take would deadlock the RX thread -> CS stalls -> "Command Disallowed"
		 * on every subsequent enable. Best-effort take is safe: we serialize ranging, so
		 * the buffer isn't being read when a new procedure starts. */
		(void)k_sem_take(&sem_distance_estimate_updated, K_NO_WAIT);
		memset(iq.scratch_mem, 0, sizeof(iq.scratch_mem));
	}
	prev_procedure_counter = result->header.procedure_counter;

	subevent_steps_parse(result);

	/* This means all the steps from the procedure have been parsed and distance estimation can
	 * run.
	 */
	if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_COMPLETE) {
		k_sem_give(&sem_subevent_results_parsed);
	}
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("Security failed: %s level %u err %d %s", addr, level, err,
			bt_security_err_to_str(err));
		return;
	}

	LOG_INF("Security changed: %s level %u", addr, level);
	k_sem_give(&sem_security);
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	(void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected to %s (err 0x%02X)", addr, err);

	if (err) {
		bt_conn_unref(conn);
		connection = NULL;
		k_sem_give(&sem_disconnected); /* let the reconnect loop retry */
	} else {
		connection = bt_conn_ref(conn);
		link_up = true;
		k_sem_give(&sem_connected);

		dk_set_led_on(CON_STATUS_LED);
	}
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02X)", reason);

	link_up = false;
	bt_conn_unref(conn);
	connection = NULL;
	dk_set_led_off(CON_STATUS_LED);

	/* No reboot (stock behaviour). The reconnect loop in main() re-scans and
	 * re-establishes, so a transient drop or pairing failure self-heals. */
	k_sem_give(&sem_disconnected);
}

static void remote_capabilities_cb(struct bt_conn *conn, uint8_t status,
				   struct bt_conn_le_cs_capabilities *params)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		static const char *const rtt_precision_str[] = {"not supported", "10 ns", "150 ns",
								"invalid"};

		uint8_t aa_idx = MIN((uint8_t)params->rtt_aa_only_precision, 3U);
		uint8_t snd_idx = MIN((uint8_t)params->rtt_sounding_precision, 3U);
		uint8_t rand_idx = MIN((uint8_t)params->rtt_random_payload_precision, 3U);

		LOG_INF("CS capability exchange completed.\n"
			"Remote CS capabilities:\n"
			" - num_config_supported: %u\n"
			" - max_consecutive_procedures_supported: %u\n"
			" - num_antennas_supported: %u\n"
			" - max_antenna_paths_supported: %u\n"
			" - initiator_supported: %s\n"
			" - reflector_supported: %s\n"
			" - mode_3_supported: %s\n"
			" - rtt_aa_only_precision: %s (%u)\n"
			" - rtt_sounding_precision: %s (%u)\n"
			" - rtt_random_payload_precision: %s (%u)\n"
			" - rtt_aa_only_n: %u\n"
			" - rtt_sounding_n: %u\n"
			" - rtt_random_payload_n: %u\n"
			" - phase_based_nadm_sounding_supported: %s\n"
			" - phase_based_nadm_random_supported: %s\n"
			" - cs_sync_2m_phy_supported: %s\n"
			" - cs_sync_2m_2bt_phy_supported: %s\n"
			" - cs_without_fae_supported: %s\n"
			" - chsel_alg_3c_supported: %s\n"
			" - pbr_from_rtt_sounding_seq_supported: %s\n"
			" - t_ip1_times_supported: 0x%04x\n"
			" - t_ip2_times_supported: 0x%04x\n"
			" - t_fcs_times_supported: 0x%04x\n"
			" - t_pm_times_supported: 0x%04x\n"
			" - t_sw_time: %u us\n"
			" - tx_snr_capability: 0x%02x\n"
			" - t_ip2_ipt_times_supported: 0x%04x\n"
			" - t_sw_ipt_time_supported: %u us\n",
			params->num_config_supported, params->max_consecutive_procedures_supported,
			params->num_antennas_supported, params->max_antenna_paths_supported,
			params->initiator_supported ? "yes" : "no",
			params->reflector_supported ? "yes" : "no",
			params->mode_3_supported ? "yes" : "no", rtt_precision_str[aa_idx],
			(uint8_t)params->rtt_aa_only_precision, rtt_precision_str[snd_idx],
			(uint8_t)params->rtt_sounding_precision, rtt_precision_str[rand_idx],
			(uint8_t)params->rtt_random_payload_precision, params->rtt_aa_only_n,
			params->rtt_sounding_n, params->rtt_random_payload_n,
			params->phase_based_nadm_sounding_supported ? "yes" : "no",
			params->phase_based_nadm_random_supported ? "yes" : "no",
			params->cs_sync_2m_phy_supported ? "yes" : "no",
			params->cs_sync_2m_2bt_phy_supported ? "yes" : "no",
			params->cs_without_fae_supported ? "yes" : "no",
			params->chsel_alg_3c_supported ? "yes" : "no",
			params->pbr_from_rtt_sounding_seq_supported ? "yes" : "no",
			params->t_ip1_times_supported, params->t_ip2_times_supported,
			params->t_fcs_times_supported, params->t_pm_times_supported,
			params->t_sw_time, params->tx_snr_capability,
			params->t_ip2_ipt_times_supported, params->t_sw_ipt_time_supported);

		k_sem_give(&sem_remote_capabilities_obtained);
	} else {
		LOG_WRN("CS capability exchange failed. (HCI status 0x%02x)", status);
	}
}

static void config_create_cb(struct bt_conn *conn, uint8_t status,
			     struct bt_conn_le_cs_config *config)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		cs_config = *config;

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
		k_sem_give(&sem_cs_security_enabled);
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
			/* count=1 burst finished and the procedure auto-disabled; the ranging
			 * loop waits for this before the next turn's enable (avoids -EACCES). */
			k_sem_give(&sem_procedure_disabled);
		}
	} else {
		LOG_WRN("CS procedures enable failed. (HCI status 0x%02x)", status);
	}
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
	int err;

	LOG_INF("Connecting failed, restarting scanning");

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
	if (err) {
		LOG_ERR("Failed to restart scanning (err %i)", err);
		return;
	}
}

static void scan_connecting(struct bt_scan_device_info *device_info, struct bt_conn *conn)
{
	LOG_INF("Connecting");
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL, scan_connecting_error, scan_connecting);

static int scan_init(struct bt_scan_init_param *p_param)
{
	int err;

	bt_scan_init(p_param);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_NAME, REFLECTOR_NAME);
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

static void cs_config_get(struct bt_le_cs_create_config_params *config_params)
{
	config_params->id = CS_CONFIG_ID;
	config_params->mode = BT_CONN_LE_CS_MAIN_MODE_2_NO_SUB_MODE;
	config_params->min_main_mode_steps = 2;
	config_params->max_main_mode_steps = 5;
	config_params->main_mode_repetition = 0;
	config_params->mode_0_steps = NUM_MODE_0_STEPS;
	config_params->role = BT_CONN_LE_CS_ROLE_INITIATOR;
	config_params->rtt_type = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY;
	config_params->cs_sync_phy = BT_CONN_LE_CS_SYNC_1M_PHY;
	config_params->channel_map_repetition = 1;
	config_params->channel_selection_type = BT_CONN_LE_CS_CHSEL_TYPE_3B;
	config_params->ch3c_shape = BT_CONN_LE_CS_CH3C_SHAPE_HAT;
	config_params->ch3c_jump = 2;
	/* Enable inline PCT transfer. */
	config_params->cs_enhancements_1 = 1;
}

int main(void)
{
	int err;

	LOG_INF("Starting Channel Sounding IPT Initiator Sample");

	dk_leds_init();

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	/* The round-robin serializes CS (one anchor ranges at a time), so the per-anchor
	 * interval de-correlation we needed for free-running contention is no longer
	 * required. Use a SHORT fixed interval: the ~58-step procedure is delivered over
	 * many connection events, so a short interval makes each procedure COMPLETE much
	 * faster (same tone count), which is what limits the round-robin rate.
	 * (hash is still used below to stagger scan start across anchors at boot.) */
	bt_addr_le_t ids[CONFIG_BT_ID_MAX];
	size_t id_count = ARRAY_SIZE(ids);
	uint8_t hash = 0;

	bt_id_get(ids, &id_count);
	if (id_count > 0) {
		for (int i = 0; i < 6; i++) {
			hash ^= ids[0].a.val[i];
		}
	}

	/* 16 units * 1.25 ms = 20 ms (subevent_len 8 ms fits; > overhead). */
	uint16_t conn_interval_units = 16;

	LOG_INF("Anchor connection interval: %u units (%u.%02u ms)", conn_interval_units,
		(conn_interval_units * 125) / 100, (conn_interval_units * 125) % 100);

	static struct bt_le_conn_param conn_param;

	conn_param.interval_min = conn_interval_units;
	conn_param.interval_max = conn_interval_units;
	conn_param.latency = 0;
	conn_param.timeout = BT_GAP_MS_TO_CONN_TIMEOUT(4000);

	struct bt_scan_init_param scan_params = {
		.scan_param = NULL,
		.conn_param = &conn_param,
		.connect_if_match = 1};

	err = scan_init(&scan_params);
	if (err) {
		LOG_ERR("Scan init failed (err %d)", err);
		return 0;
	}

	/* Stagger the start of scanning per-anchor (address-derived), so multiple anchors
	 * powering on together don't all connect+pair at the same instant. The simultaneous
	 * pairing storm made some anchors fail security (err 9) and (since the stock app
	 * reboots on disconnect) reboot-loop. A fixed per-device offset de-synchronises them
	 * so pairing succeeds. */
	k_msleep((hash % 6) * 200);

	/* CS configuration + procedure params (computed once; independent of the link). */
	const struct bt_le_cs_set_default_settings_param default_settings = {
		.enable_initiator_role = true,
		.enable_reflector_role = false,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	struct bt_le_cs_create_config_params config_params;

	cs_config_get(&config_params);
	bt_le_cs_set_valid_chmap_bits(config_params.channel_map);

	/* scale factor of conn_interval units to proc_interval units is 1.25/0.625 = 2.
	 * Fairness is handled by the reflector round-robin (one anchor ranges per turn),
	 * so each turn needs ONE bounded procedure (max_procedure_count=1). */
	const uint16_t acl_interval_in_proc_interval_units = conn_interval_units * 2;
	const uint16_t desired_procedure_interval = 2;
	const uint16_t desired_max_procedure_length =
		acl_interval_in_proc_interval_units * (desired_procedure_interval - 1);

	const struct bt_le_cs_set_procedure_parameters_param procedure_params = {
		.config_id = CS_CONFIG_ID,
		.max_procedure_len = desired_max_procedure_length,
		.min_procedure_interval = desired_procedure_interval,
		.max_procedure_interval = desired_procedure_interval,
		.max_procedure_count = 1,
		.min_subevent_len = 8000,
		.max_subevent_len = 8000,
		.tone_antenna_config_selection = BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
		.phy = BT_LE_CS_PROCEDURE_PHY_2M,
		.tx_power_delta = 0x80,
		.preferred_peer_antenna = BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1,
		.snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
		.snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
	};

	const struct bt_le_cs_procedure_enable_param enable_params = {
		.config_id = CS_CONFIG_ID,
		.enable = 1,
	};
	const uint8_t done = 0;

	/* Reconnect state machine: scan -> connect -> secure -> CS setup -> subscribe ->
	 * range (until link drops) -> recover -> repeat. Never reboots, and every wait is
	 * bounded so a mid-setup disconnect can't hang. */
	while (true) {
		link_up = false;
		k_sem_reset(&sem_connected);
		k_sem_reset(&sem_security);
		k_sem_reset(&sem_remote_capabilities_obtained);
		k_sem_reset(&sem_config_created);
		k_sem_reset(&sem_cs_security_enabled);
		k_sem_reset(&sem_discovered);
		k_sem_reset(&sem_turn);
		k_sem_reset(&sem_disconnected);
		turn_value_handle = 0;

		if (bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE) != 0) {
			k_msleep(500);
			continue;
		}
		if (k_sem_take(&sem_connected, K_SECONDS(15)) != 0) {
			(void)bt_scan_stop();
			continue;
		}
		(void)bt_scan_stop();

		/* Encrypt the link (CS requires it). Retry a few times: under a multi-anchor
		 * pairing storm the first attempt can fail (security err 9). */
		bool secured = false;

		for (int attempt = 0; attempt < 3 && link_up; attempt++) {
			if (bt_conn_set_security(connection, BT_SECURITY_L2) == 0 &&
			    k_sem_take(&sem_security, K_SECONDS(4)) == 0) {
				secured = true;
				break;
			}
			LOG_WRN("Security attempt %d failed, retrying", attempt + 1);
			k_msleep(200);
		}
		if (!secured) {
			goto recover;
		}

		/* CS setup — each step bounded; abort to recover on link loss or timeout. */
		if (!link_up || bt_le_cs_set_default_settings(connection, &default_settings)) {
			goto recover;
		}
		if (!link_up || bt_le_cs_read_remote_supported_capabilities(connection) ||
		    k_sem_take(&sem_remote_capabilities_obtained, K_SECONDS(4))) {
			goto recover;
		}
		if (!link_up ||
		    bt_le_cs_create_config(connection, &config_params,
					   BT_LE_CS_CREATE_CONFIG_CONTEXT_LOCAL_AND_REMOTE) ||
		    k_sem_take(&sem_config_created, K_SECONDS(4))) {
			goto recover;
		}
		if (!link_up || bt_le_cs_security_enable(connection) ||
		    k_sem_take(&sem_cs_security_enabled, K_SECONDS(4))) {
			goto recover;
		}
		if (!link_up || bt_le_cs_set_procedure_parameters(connection, &procedure_params)) {
			goto recover;
		}

		/* Discover + subscribe to the reflector's CS-turn service. */
		if (!link_up ||
		    bt_gatt_dm_start(connection, &turn_svc_uuid.uuid, &turn_dm_cb, NULL) ||
		    k_sem_take(&sem_discovered, K_SECONDS(5))) {
			goto recover;
		}

		LOG_INF("Anchor ready; ranging on round-robin grants");

		/* One bounded CS burst per granted turn, until the link drops. */
		while (link_up) {
			if (k_sem_take(&sem_turn, K_SECONDS(2)) != 0) {
				continue; /* no grant yet; re-check link_up */
			}
			if (!link_up) {
				break;
			}
			k_sem_reset(&sem_subevent_results_parsed);

			/* One bounded procedure (count=1); the controller auto-disables after it.
			 * Do NOT wait on a disable event here — that extra latency turned the
			 * per-turn re-enable into "Command Disallowed" (0x0c) and killed ranging.
			 * An occasional re-enable error just costs one turn (reflector advances). */
			if (bt_le_cs_procedure_enable(connection, &enable_params) == 0) {
				/* Wait for the procedure to COMPLETE. At the 20 ms interval it finishes
				 * in ~200-300 ms; 700 ms covers it with margin. (Must exceed the
				 * procedure time or the COMPLETE is missed -> 0 DIST; but keep it tight so
				 * an aborted turn doesn't stall the rotation.) */
				if (k_sem_take(&sem_subevent_results_parsed, K_MSEC(700)) == 0) {
					distance_estimates_update();
				}
			}

			if (turn_value_handle) {
				(void)bt_gatt_write_without_response(connection, turn_value_handle,
								     &done, sizeof(done), false);
			}
		}

recover:
		if (link_up && connection) {
			bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
		(void)k_sem_take(&sem_disconnected, K_SECONDS(3));
		/* Small staggered backoff so anchors don't all re-storm together. */
		k_msleep(200 + (hash % 6) * 100);
	}

	return 0;
}
