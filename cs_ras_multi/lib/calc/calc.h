#pragma once

#include "../global.h"

/* Distance estimation is initiator-only. Keep the cs_de dependency (and its
 * CONFIG_BT_CS_DE_MAX_NUM_ANTENNA_PATHS-sized types) out of the reflector build. */
#if BUILD_INITIATOR

#include <zephyr/kernel.h>
#include <math.h>
#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <bluetooth/services/ras.h>
#include <bluetooth/cs_de.h>

void store_distance_estimates(cs_de_report_t *p_report);
uint8_t cs_buffer_status();
cs_de_dist_estimates_t get_distance(uint8_t ap);

/* IFFT distance estimation using Nordic's official cs_de library. */
float estimate_distance(struct net_buf_simple *local_steps, struct net_buf_simple *peer_steps,
			uint8_t n_ap, enum bt_conn_le_cs_role role, uint16_t compensation, int tag_idx);

#endif /* BUILD_INITIATOR */
