/*
 * scan_cs_soak — shared IPT/IFFT distance estimator (see cs_ifft.h).
 *
 * Lifted from the ipt_swap initiator: parse the HIGH-quality mode-2 tones from the
 * local subevent (which, with IPT, already carry the combined initiator+reflector
 * phase), run cs_de_ifft(), and report a median-filtered distance.
 */

#include <math.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/cs.h>
#include <bluetooth/cs_de.h>
#include "cs_ifft.h"

#define CHANNEL_INDEX_OFFSET            (2)
#define MEDIAN_FILTER_SIZE              (9)
/* Minimum HIGH-quality tones for an estimate to be trusted (matches ras_initiator). */
#define TONE_QI_OK_TONE_COUNT_THRESHOLD (15)

#ifndef CONFIG_APP_CS_DISTANCE_OFFSET_MM
#define CONFIG_APP_CS_DISTANCE_OFFSET_MM 0
#endif

/* Local initiator IQ samples; the scratch_mem union member is reused by cs_de_ifft. */
static union {
	struct {
		float i;
		float q;
	} values[CS_DE_NUM_CHANNELS];
	float scratch_mem[2 * CONFIG_BT_CS_DE_NFFT_SIZE];
} iq;

struct distance_estimate_buffer {
	float estimates[MEDIAN_FILTER_SIZE];
	uint8_t num_valid;
	uint8_t index;
};

static struct distance_estimate_buffer distance_estimate_buffer;

static void store_distance_estimate(float distance)
{
	distance_estimate_buffer.estimates[distance_estimate_buffer.index] = distance;
	distance_estimate_buffer.index = (distance_estimate_buffer.index + 1) % MEDIAN_FILTER_SIZE;

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
	}
	return values[count / 2];
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

static void pcts_parse(uint8_t channel_index,
		       struct bt_hci_le_cs_step_data_tone_info *local_tone_info)
{
	/* Only accept HIGH-quality tones (matches ras_initiator) — a low-quality tone
	 * biases the estimate. Skipped channels stay zero and don't count. */
	if (local_tone_info[0].quality_indicator != BT_HCI_LE_CS_TONE_QUALITY_HIGH) {
		return;
	}

	struct bt_le_cs_iq_sample local_iq =
		bt_le_cs_parse_pct(local_tone_info[0].phase_correction_term);

	iq.values[channel_index - CHANNEL_INDEX_OFFSET].i = local_iq.i;
	iq.values[channel_index - CHANNEL_INDEX_OFFSET].q = local_iq.q;
}

static void subevent_steps_parse(struct bt_conn_le_cs_subevent_result *result)
{
	for (uint8_t i = 0; i < result->header.num_steps_reported; i++) {
		if (result->step_data_buf->len < 3) {
			return;
		}

		struct bt_le_cs_subevent_step local_step = {0};

		local_step.mode = net_buf_simple_pull_u8(result->step_data_buf);
		local_step.channel = net_buf_simple_pull_u8(result->step_data_buf);
		local_step.data_len = net_buf_simple_pull_u8(result->step_data_buf);

		if (local_step.data_len == 0 || local_step.data_len > result->step_data_buf->len) {
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

static void distance_estimates_update(void)
{
	uint8_t samples = 0;

	/* Count HIGH-quality tones BEFORE cs_de_ifft(), which overwrites iq.values. */
	for (uint8_t i = 0; i < CS_DE_NUM_CHANNELS; i++) {
		if (iq.values[i].i != 0.0f || iq.values[i].q != 0.0f) {
			samples++;
		}
	}

	if (samples < TONE_QI_OK_TONE_COUNT_THRESHOLD) {
		printk("DROP:AP:0,SAMPLES:%d\n", samples);
		return;
	}

	float distance_ifft = cs_de_ifft(iq.scratch_mem);

	distance_ifft -= (float)CONFIG_APP_CS_DISTANCE_OFFSET_MM / 1000.0f;
	if (distance_ifft < 0.0f) {
		distance_ifft = 0.0f;
	}

	if (isfinite(distance_ifft)) {
		store_distance_estimate(distance_ifft);
		float distance_median = get_filtered_distance();

		printk("DIST:%.3f,AP:0,SAMPLES:%d\n", (double)distance_median, samples);
	}
}

bool cs_ifft_accumulate(struct bt_conn_le_cs_subevent_result *result)
{
	static uint32_t prev_procedure_counter = UINT16_MAX + 1;

	const bool cs_aborted = (result->header.procedure_abort_reason !=
				 BT_HCI_LE_CS_PROCEDURE_ABORT_REASON_NO_ABORT) ||
				(result->header.subevent_abort_reason !=
				 BT_HCI_LE_CS_SUBEVENT_ABORT_REASON_NO_ABORT);

	if (cs_aborted) {
		return false;
	}

	if (result->header.procedure_counter != prev_procedure_counter) {
		memset(iq.scratch_mem, 0, sizeof(iq.scratch_mem));
	}
	prev_procedure_counter = result->header.procedure_counter;

	subevent_steps_parse(result);

	return result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_COMPLETE;
}

void cs_ifft_compute_print(void)
{
	distance_estimates_update();

	/* cs_de_ifft() overwrote the IQ buffer (it shares a union with scratch_mem).
	 * Clear it so the next procedure accumulates from zero. The CS procedure_counter
	 * restarts at 0 on every new connection in this soak, so the counter-change
	 * reset in cs_ifft_accumulate() cannot be relied on to do it. */
	memset(iq.scratch_mem, 0, sizeof(iq.scratch_mem));
}
