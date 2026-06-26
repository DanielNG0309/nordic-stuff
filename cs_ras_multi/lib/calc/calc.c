#include "calc.h"

/* Official cs_de IFFT distance-estimation path.
 *
 * This uses Nordic's supported cs_de estimator (IFFT / phase-slope / RTT) in place
 * of a closed-source phase-slope library. It mirrors
 * the parse-and-estimate logic of the official RAS initiator sample
 * (nrf/samples/bluetooth/channel_sounding/ras_initiator/src/main.c), feeding the
 * existing sliding-window buffer in this file.
 *
 * The whole unit is initiator-only: only the RREQ (initiator) side estimates
 * distance, so the reflector build compiles this to an empty translation unit
 * and needs no cs_de / CMSIS-DSP dependency.
 */

#if BUILD_INITIATOR

#include <stdlib.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

static K_MUTEX_DEFINE(distance_estimate_buffer_mutex);

static uint8_t buffer_index;
static uint8_t buffer_num_valid;
static cs_de_dist_estimates_t distance_estimate_buffer[MAX_AP][DE_SLIDING_WINDOW_SIZE];

uint8_t cs_buffer_status() {
	return buffer_num_valid;
}

static int float_cmp(const void *a, const void *b)
{
	float fa = *(const float *)a;
	float fb = *(const float *)b;

	return (fa > fb) - (fa < fb);
}

/* Median over the valid samples (sorts in place). Median rejects the occasional
 * multipath/outlier estimate far better than a mean, which a single bad reading
 * drags off. Returns NAN if no valid samples. */
static float median_inplace(int count, float *values)
{
	if (count == 0) {
		return NAN;
	}

	qsort(values, count, sizeof(float), float_cmp);

	if (count % 2 == 0) {
		return (values[count / 2] + values[count / 2 - 1]) / 2.0f;
	}

	return values[count / 2];
}

cs_de_dist_estimates_t get_distance(uint8_t ap) {
	cs_de_dist_estimates_t result = {};
	uint8_t num_ifft = 0;
	uint8_t num_phase_slope = 0;
	uint8_t num_rtt = 0;

	float temp_ifft[DE_SLIDING_WINDOW_SIZE];
	float temp_phase_slope[DE_SLIDING_WINDOW_SIZE];
	float temp_rtt[DE_SLIDING_WINDOW_SIZE];

	int lock_state = k_mutex_lock(&distance_estimate_buffer_mutex, K_FOREVER);

	__ASSERT_NO_MSG(lock_state == 0);

	for (uint8_t i = 0; i < buffer_num_valid; i++) {
		if (isfinite(distance_estimate_buffer[ap][i].ifft)) { // isfinite i.e is a real number check
			temp_ifft[num_ifft++] = distance_estimate_buffer[ap][i].ifft;
		}
		if (isfinite(distance_estimate_buffer[ap][i].phase_slope)) {
			temp_phase_slope[num_phase_slope++] = distance_estimate_buffer[ap][i].phase_slope;
		}
		if (isfinite(distance_estimate_buffer[ap][i].rtt)) {
			temp_rtt[num_rtt++] = distance_estimate_buffer[ap][i].rtt;
		}
	}

	k_mutex_unlock(&distance_estimate_buffer_mutex);

	result.ifft = median_inplace(num_ifft, temp_ifft);
	result.phase_slope = median_inplace(num_phase_slope, temp_phase_slope);
	result.rtt = median_inplace(num_rtt, temp_rtt);

	return result;
}

void store_distance_estimates(cs_de_report_t *p_report) {
	int lock_state = k_mutex_lock(&distance_estimate_buffer_mutex, K_FOREVER);

	__ASSERT_NO_MSG(lock_state == 0);

	for (uint8_t ap = 0; ap < p_report->n_ap; ap++) {
		memcpy(&distance_estimate_buffer[ap][buffer_index],
		       &p_report->distance_estimates[ap], sizeof(cs_de_dist_estimates_t));
	}

	buffer_index = (buffer_index + 1) % DE_SLIDING_WINDOW_SIZE;

	if (buffer_num_valid < DE_SLIDING_WINDOW_SIZE) {
		buffer_num_valid++;
	}

	k_mutex_unlock(&distance_estimate_buffer_mutex);
}

/* For RSSI */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/byteorder.h>

#define CHANNEL_INDEX_OFFSET            (2)
#define TONE_QI_OK_TONE_COUNT_THRESHOLD (15)

/* Connection used to read RSSI for the DIST: output line (defined in src/initator.c). */
extern struct bt_conn *conn;

/* Per-procedure scratch for building the cs_de report. */
static uint16_t m_n_iqs[CONFIG_BT_RAS_MAX_ANTENNA_PATHS][CS_DE_NUM_CHANNELS];
static cs_de_report_t m_cs_de_report;

/**********************************************************************/
/*                 RSSI helper (for DIST: output line)               */
/**********************************************************************/

static void read_conn_rssi(struct bt_conn *c, int8_t *out_rssi)
{
	uint16_t handle;
	struct net_buf *buf, *rsp = NULL;
	struct bt_hci_cp_read_rssi *cp;
	struct bt_hci_rp_read_rssi *rp;
	int err;

	*out_rssi = 0;

	if (!c) {
		return;
	}

	err = bt_hci_get_conn_handle(c, &handle);
	if (err) {
		LOG_ERR("No connection handle (err %d)", err);
		return;
	}

	buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(*cp));
	if (!buf) {
		LOG_ERR("Unable to allocate command buffer");
		return;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);

	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
	if (err) {
		LOG_ERR("Read RSSI err: %d", err);
		return;
	}

	rp = (void *)rsp->data;
	*out_rssi = rp->rssi;

	net_buf_unref(rsp);
}

/* Path-loss RSSI->distance fallback, kept so the DIST: line carries an RSSI_DIST
 * field the multilateration tool can weight against. Calibrated for short range. */
static float calculate_rssi_distance(int8_t rssi)
{
	const float reference_rssi = -60.0f; /* RSSI at 1 m */
	const float n = 2.2f;                /* indoor short-range path-loss exponent */

	if (rssi > -30) {
		rssi = -30;
	}
	if (rssi < -90) {
		rssi = -90;
	}

	float exponent = (reference_rssi - rssi) / (10.0f * n);
	float distance = powf(10.0f, exponent);

	if (distance < 0.1f) {
		distance = 0.1f;
	}
	if (distance > 10.0f) {
		distance = 10.0f;
	}

	return distance;
}

/**********************************************************************/
/*        RAS step parsing -> cs_de_report_t (from official sample)  */
/**********************************************************************/

static void cumulate_mean(float *avg, float new_value, uint16_t *N)
{
	float a = 1.0f / (*N);
	float b = 1.0f - a;
	*avg = a * new_value + b * (*avg);
}

static void extract_pcts(cs_de_report_t *p_report, uint8_t channel_index,
			 uint8_t antenna_permutation_index,
			 struct bt_hci_le_cs_step_data_tone_info *local_tone_info,
			 struct bt_hci_le_cs_step_data_tone_info *remote_tone_info)
{
	for (uint8_t tone_index = 0; tone_index < p_report->n_ap; tone_index++) {
		int antenna_path = bt_le_cs_get_antenna_path(p_report->n_ap,
							     antenna_permutation_index, tone_index);
		if (antenna_path < 0) {
			LOG_WRN("Invalid antenna path.");
			return;
		}

		if (local_tone_info[tone_index].quality_indicator !=
			    BT_HCI_LE_CS_TONE_QUALITY_HIGH ||
		    remote_tone_info[tone_index].quality_indicator !=
			    BT_HCI_LE_CS_TONE_QUALITY_HIGH) {
			continue;
		}

		struct bt_le_cs_iq_sample local_iq =
			bt_le_cs_parse_pct(local_tone_info[tone_index].phase_correction_term);
		struct bt_le_cs_iq_sample remote_iq =
			bt_le_cs_parse_pct(remote_tone_info[tone_index].phase_correction_term);

		m_n_iqs[antenna_path][channel_index]++;

		if (m_n_iqs[antenna_path][channel_index] == 1) {
			p_report->iq_tones[antenna_path].i_local[channel_index] = local_iq.i;
			p_report->iq_tones[antenna_path].q_local[channel_index] = local_iq.q;
			p_report->iq_tones[antenna_path].i_remote[channel_index] = remote_iq.i;
			p_report->iq_tones[antenna_path].q_remote[channel_index] = remote_iq.q;
		} else {
			cumulate_mean(&p_report->iq_tones[antenna_path].i_local[channel_index],
				      local_iq.i, &m_n_iqs[antenna_path][channel_index]);
			cumulate_mean(&p_report->iq_tones[antenna_path].q_local[channel_index],
				      local_iq.q, &m_n_iqs[antenna_path][channel_index]);
			cumulate_mean(&p_report->iq_tones[antenna_path].i_remote[channel_index],
				      remote_iq.i, &m_n_iqs[antenna_path][channel_index]);
			cumulate_mean(&p_report->iq_tones[antenna_path].q_remote[channel_index],
				      remote_iq.q, &m_n_iqs[antenna_path][channel_index]);
		}
	}
}

static void extract_rtt_timings(cs_de_report_t *p_report,
				struct bt_hci_le_cs_step_data_mode_1 *local_rtt_data,
				struct bt_hci_le_cs_step_data_mode_1 *peer_rtt_data)
{
	if (local_rtt_data->packet_quality_aa_check !=
		    BT_HCI_LE_CS_PACKET_QUALITY_AA_CHECK_SUCCESSFUL ||
	    local_rtt_data->packet_rssi == BT_HCI_LE_CS_PACKET_RSSI_NOT_AVAILABLE ||
	    local_rtt_data->tod_toa_reflector == BT_HCI_LE_CS_TIME_DIFFERENCE_NOT_AVAILABLE ||
	    peer_rtt_data->packet_quality_aa_check !=
		    BT_HCI_LE_CS_PACKET_QUALITY_AA_CHECK_SUCCESSFUL ||
	    peer_rtt_data->packet_rssi == BT_HCI_LE_CS_PACKET_RSSI_NOT_AVAILABLE ||
	    peer_rtt_data->tod_toa_reflector == BT_HCI_LE_CS_TIME_DIFFERENCE_NOT_AVAILABLE) {
		return;
	}

	if (p_report->role == BT_CONN_LE_CS_ROLE_INITIATOR) {
		p_report->rtt_accumulated_half_ns +=
			local_rtt_data->toa_tod_initiator - peer_rtt_data->tod_toa_reflector;
	} else {
		p_report->rtt_accumulated_half_ns +=
			peer_rtt_data->toa_tod_initiator - local_rtt_data->tod_toa_reflector;
	}

	p_report->rtt_count++;
}

static bool process_ranging_header(struct ras_ranging_header *ranging_header, void *user_data)
{
	cs_de_report_t *p_report = (cs_de_report_t *)user_data;

	p_report->n_ap = MAX(1, ((ranging_header->antenna_paths_mask & BIT(0)) +
				 ((ranging_header->antenna_paths_mask & BIT(1)) >> 1) +
				 ((ranging_header->antenna_paths_mask & BIT(2)) >> 2) +
				 ((ranging_header->antenna_paths_mask & BIT(3)) >> 3)));
	return true;
}

static bool process_step_data(struct bt_le_cs_subevent_step *local_step,
			      struct bt_le_cs_subevent_step *peer_step, void *user_data)
{
	cs_de_report_t *p_report = (cs_de_report_t *)user_data;

	if (local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_2) {
		struct bt_hci_le_cs_step_data_mode_2 *local_step_data =
			(struct bt_hci_le_cs_step_data_mode_2 *)local_step->data;
		struct bt_hci_le_cs_step_data_mode_2 *peer_step_data =
			(struct bt_hci_le_cs_step_data_mode_2 *)peer_step->data;

		extract_pcts(p_report, local_step->channel - CHANNEL_INDEX_OFFSET,
			     local_step_data->antenna_permutation_index, local_step_data->tone_info,
			     peer_step_data->tone_info);
	} else if (local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_1) {
		struct bt_hci_le_cs_step_data_mode_1 *local_step_data =
			(struct bt_hci_le_cs_step_data_mode_1 *)local_step->data;
		struct bt_hci_le_cs_step_data_mode_1 *peer_step_data =
			(struct bt_hci_le_cs_step_data_mode_1 *)peer_step->data;

		extract_rtt_timings(p_report, local_step_data, peer_step_data);
	} else if (local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_3) {
		struct bt_hci_le_cs_step_data_mode_3 *local_step_data =
			(struct bt_hci_le_cs_step_data_mode_3 *)local_step->data;
		struct bt_hci_le_cs_step_data_mode_3 *peer_step_data =
			(struct bt_hci_le_cs_step_data_mode_3 *)peer_step->data;

		extract_pcts(p_report, local_step->channel - CHANNEL_INDEX_OFFSET,
			     local_step_data->antenna_permutation_index, local_step_data->tone_info,
			     peer_step_data->tone_info);

		extract_rtt_timings(p_report,
				    (struct bt_hci_le_cs_step_data_mode_1 *)local_step_data,
				    (struct bt_hci_le_cs_step_data_mode_1 *)peer_step_data);
	}

	return true;
}

/* Count tones that contributed at least one IQ sample on the given antenna path,
 * used as the SAMPLES field of the DIST: output line. */
static uint8_t count_tone_samples(uint8_t ap)
{
	uint8_t samples = 0;

	for (uint8_t ch = 0; ch < CS_DE_NUM_CHANNELS; ch++) {
		if (m_n_iqs[ap][ch] > 0) {
			samples++;
		}
	}

	return samples;
}

/**********************************************************************/
/*                       DISTANCE ESTIMATION                          */
/**********************************************************************/

/* Drop-in replacement for the black-box estimate_distance(): parses the RAS
 * step buffers into a cs_de_report_t, runs the official IFFT/phase-slope/RTT
 * estimator, feeds the sliding-window buffer, and emits the DIST: line the
 * multilateration tool parses. Returns the windowed IFFT distance for AP 0. */
float estimate_distance(struct net_buf_simple *local_steps, struct net_buf_simple *peer_steps,
			uint8_t n_ap, enum bt_conn_le_cs_role role, uint16_t compensation, int tag_idx)
{
	ARG_UNUSED(n_ap);
	ARG_UNUSED(compensation);
	ARG_UNUSED(tag_idx);

	memset(m_n_iqs, 0, sizeof(m_n_iqs));
	memset(&m_cs_de_report, 0, sizeof(m_cs_de_report));
	m_cs_de_report.role = role;

	bt_ras_rreq_rd_subevent_data_parse(peer_steps, local_steps, m_cs_de_report.role,
					   process_ranging_header, NULL, process_step_data,
					   &m_cs_de_report);

	cs_de_quality_t quality = cs_de_calc(&m_cs_de_report);

	if (quality != CS_DE_QUALITY_OK) {
		/* Diagnostic: a completed ranging whose IFFT/phase/RTT all failed cs_de's
		 * internal quality gate. Previously dropped silently — which read as a
		 * "stall" (no DIST, no error) on the serial output. Log the raw per-method
		 * estimates and tone count so we can see why (NaN ifft, too few good tones…). */
		printk("DROP:quality=DO_NOT_USE,AP:%d,SAMPLES:%d,ifft:%.3f,phase:%.3f,rtt:%.3f\n",
		       m_cs_de_report.n_ap, count_tone_samples(0),
		       (double)m_cs_de_report.distance_estimates[0].ifft,
		       (double)m_cs_de_report.distance_estimates[0].phase_slope,
		       (double)m_cs_de_report.distance_estimates[0].rtt);
		return 0.0f;
	}

	store_distance_estimates(&m_cs_de_report);

	int8_t rssi = 0;
	read_conn_rssi(conn, &rssi);

	float dist = 0.0f;

	for (uint8_t ap = 0; ap < m_cs_de_report.n_ap; ap++) {
		cs_de_dist_estimates_t est = get_distance(ap);
		uint8_t samples_cnt = count_tone_samples(ap);

		/* Keep the exact line format the Multilateration tool parses:
		 * DIST:%.3f,AP:%d,SAMPLES:%d,RSSI:%d,RSSI_DIST:%.3f */
		printk("DIST:%.3f,AP:%d,SAMPLES:%d,RSSI:%d,RSSI_DIST:%.3f\n",
		       (double)est.ifft, ap, samples_cnt, rssi,
		       (double)calculate_rssi_distance(rssi));

		if (ap == 0) {
			dist = est.ifft;
		}
	}

	return dist;
}

#endif /* BUILD_INITIATOR */
