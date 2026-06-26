#include "cs.h"
#include <zephyr/bluetooth/conn.h>
#include <string.h>
#include "../sync/sync.h"

#if BUILD_INITIATOR 
struct cs_semaphores_ctx sem_cs; 
struct cs_count_ctx counter; 
extern int ble_write(bool state);
extern int sync_signal_completion(write_func_t write_fn);

static struct bt_le_cs_create_config_params config_params = {
    .id = ID,
    /* NCS 3.3.1: main_mode_type + sub_mode_type were merged into a single
     * combined `mode` field. Was main_mode 2 (PBR) + sub_mode 1 (RTT). */
    .mode = BT_CONN_LE_CS_MAIN_MODE_2_SUB_MODE_1,
    /* IFFT needs many high-quality tones across the channel map (Nordic's quality
     * gate is ~15). The old values (3 main steps, 1 mode-0 step) were tuned for the
     * phase-slope black box and yielded only 5-7 tones. Match the official IFFT
     * sample (5 main steps, 3 mode-0 steps) and repeat the channel map so one
     * bounded procedure sweeps more of the 75 channels. Tune empirically on SAMPLES. */
    .min_main_mode_steps = 2,
    .max_main_mode_steps = 5,
    .main_mode_repetition = 0,
    .mode_0_steps = 3,
    .role = BT_CONN_LE_CS_ROLE_INITIATOR,
    .rtt_type =  BT_CONN_LE_CS_RTT_TYPE_AA_ONLY,
    .cs_sync_phy = BT_CONN_LE_CS_SYNC_2M_PHY,
    .channel_map_repetition = 3,
    .channel_selection_type = BT_CONN_LE_CS_CHSEL_TYPE_3B,
    .ch3c_shape = BT_CONN_LE_CS_CH3C_SHAPE_HAT,
    .ch3c_jump = 2,
};

// CI >= N * Spacing (CONFIG_[...]_ACL_EVENT_SPACING_DEFAULT)
// Spacing >= T_ACL + T_CS (TCL = CONFIG_[...]_EVENT_LEN_DEFAULT, T_CS = max_procedure_len)
static const struct bt_le_cs_set_procedure_parameters_param procedure_params = {
	.config_id = ID,
	/* Longer procedure + subevent so a single round-robin turn sounds enough
	 * channels for a reliable IFFT (was 3 / 1500us -> ~6 tones). Kept bounded
	 * (max_procedure_count=1) for round-robin; subevent moderated to fit the
	 * 6-connection schedule and the 4800-byte reassembly buffer. If SAMPLES<15,
	 * raise subevent_len/max_procedure_len; if procedure-enable fails or steps
	 * overflow the reassembly buffer, lower them. */
	.max_procedure_len = 12,
	.min_procedure_interval = 1,
	.max_procedure_interval = 2,
	.max_procedure_count = 1,
	.min_subevent_len = 5000,
	.max_subevent_len = 5000,
	.tone_antenna_config_selection = BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
	.phy = BT_LE_CS_PROCEDURE_PHY_2M, 
	.tx_power_delta = 0x80,
	.preferred_peer_antenna = BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1,
	.snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
	.snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
};

__aligned(4) NET_BUF_SIMPLE_DEFINE_STATIC(latest_local_steps, LOCAL_PROCEDURE_MEM);
__aligned(4) NET_BUF_SIMPLE_DEFINE_STATIC(latest_peer_steps, BT_RAS_PROCEDURE_MEM);

#if USE_PSEUDO
__aligned(4) static uint8_t saved_local_steps_mem[LOCAL_PROCEDURE_MEM];
__aligned(4) static uint8_t saved_peer_steps_mem[BT_RAS_PROCEDURE_MEM];
static uint16_t saved_local_steps_len;
static uint16_t saved_peer_steps_len;
#endif

#endif 

#if BUILD_REFLECTOR 
struct cs_ctx ctx[CONFIG_BT_MAX_CONN]; 
#endif 

static const struct bt_le_cs_set_default_settings_param default_settings = {
	#if BUILD_INITIATOR
    .enable_initiator_role = true,
    .enable_reflector_role = false, 
	#else
	.enable_initiator_role = false,
	.enable_reflector_role = true,
	#endif 

    .cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
    .max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER
};

/**********************************************************************/
/*                           CALLBACKS                                */
/**********************************************************************/

BT_CONN_CB_DEFINE(cs_callbacks) = {
	.le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
	.le_cs_config_complete = config_create_cb,
	.le_cs_security_enable_complete = security_enable_cb,
	.le_cs_procedure_enable_complete = procedure_enable_cb,

	#if BUILD_INITIATOR 
	.le_cs_subevent_data_available = subevent_result_cb, 
	#endif 
};


void remote_capabilities_cb(struct bt_conn *conn, 
	uint8_t status, struct bt_conn_le_cs_capabilities *params) {
	
	#if BUILD_INITIATOR 
	ARG_UNUSED(conn); 
	#endif  
	ARG_UNUSED(params); 

    if (PRINT_VERBOSE) printk("[CS] caps-complete: status=0x%02x conn=%d\n", status, bt_conn_index(conn));
    if (status == BT_HCI_ERR_SUCCESS) {
		printk("CS capability exchange completed.\n");
		TRY(bt_le_cs_set_default_settings(conn, &default_settings));
	} else {
		printk("CS capability exchange failed. (HCI status 0x%02x)\n", status);
	}

	#if BUILD_INITIATOR 
	k_sem_give(&sem_cs.rrq_capabilites);
	#else 
	int idx = bt_conn_index(conn);
	bool is_valid_conn_index = (idx >= 0 && idx < CONFIG_BT_MAX_CONN);

	if (!is_valid_conn_index) {
		printk("Invalid index\n");
		return; 
	}
	k_sem_give(&ctx[bt_conn_index(conn)].sem_cs.rrq_capabilites); 
	#endif 
}

void security_enable_cb(struct bt_conn *conn, uint8_t status) {
	#if BUILD_INITIATOR 
	ARG_UNUSED(conn); 
	#endif  

    if (PRINT_VERBOSE) printk("[CS] security: %s conn=%d\n", status==BT_HCI_ERR_SUCCESS?"enabled":"FAIL", bt_conn_index(conn));
    if (status == BT_HCI_ERR_SUCCESS) {
		printk("CS security enabled.\n");

		#if BUILD_INITIATOR 
		k_sem_give(&sem_cs.cs_security);
		#else 
		k_sem_give(&ctx[bt_conn_index(conn)].sem_cs.cs_security); 
		#endif 

	} else {
		printk("CS security enable failed. (HCI status 0x%02x)\n", status);
	}
}

void config_create_cb(struct bt_conn *conn, uint8_t status, struct bt_conn_le_cs_config *config) {
	#if BUILD_INITIATOR 
	ARG_UNUSED(conn); 
	#endif  

    if (PRINT_VERBOSE) printk("[CS] config-complete: status=0x%02x id=%d conn=%d\n", status, config->id, bt_conn_index(conn));
    if (status == BT_HCI_ERR_SUCCESS) {
        printk("CS config creation complete. ID: %d\n", config->id);
		#if BUILD_INITIATOR 
		k_sem_give(&sem_cs.create_config);
		#else 
		TRY(bt_le_cs_security_enable(conn));
		k_sem_give(&ctx[bt_conn_index(conn)].sem_cs.create_config); 
		#endif 
	} else {
		printk("CS config creation failed. (HCI status 0x%02x)\n", status);
	}
}

void procedure_enable_cb(struct bt_conn *conn, uint8_t status, struct bt_conn_le_cs_procedure_enable_complete *params) {
	ARG_UNUSED(conn);

    if (PRINT_VERBOSE) printk("[CS] proc-enable: status=0x%02x state=%u conn=%d\n", status, params?params->state:255, bt_conn_index(conn));
    if (status != BT_HCI_ERR_SUCCESS) {
		printk("CS procedures enable failed. (HCI status 0x%02x)\n", status);

		#if BUILD_INITIATOR 
		k_sem_give(&sem_cs.procedure_done);
		#endif 
		return; 
	}

	if (params->state == 1) {
		#if BUILD_REFLECTOR 
        if (PRINT_VERBOSE) printk("[CS] procedures enabled conn %d.\n", bt_conn_index(conn)); 
		#endif  
	} else {
        if (PRINT_VERBOSE) printk("[CS] procedures disabled.\n");
		#if BUILD_INITIATOR 
		k_sem_give(&sem_cs.procedure_disabled); 
		k_sem_give(&sem_cs.procedure_done);
		#endif 
	}
}

#if BUILD_INITIATOR
void subevent_result_cb(struct bt_conn *conn, struct bt_conn_le_cs_subevent_result *result) {
	counter.latest_frequency_compensation = result->header.frequency_compensation;

	if (counter.dropped_ranging == result->header.procedure_counter) {
		return;
	}

	bool usable_subevent = 	
		result->header.subevent_done_status != BT_CONN_LE_CS_SUBEVENT_ABORTED; 
	

    if (PRINT_VERBOSE) printk("[CS][INIT] subevent: pc=%u done=%u sub_done=%u n_ap=%u step_len=%d\n",
           result->header.procedure_counter,
           result->header.procedure_done_status,
           result->header.subevent_done_status,
           result->header.num_antenna_paths,
           result->step_data_buf ? result->step_data_buf->len : -1);

    if (usable_subevent && result->step_data_buf) {
		if (result->step_data_buf->len <= net_buf_simple_tailroom(&latest_local_steps)) {
			uint16_t len = result->step_data_buf->len;
			uint8_t *step_data = net_buf_simple_pull_mem(result->step_data_buf, len);
			net_buf_simple_add_mem(&latest_local_steps, step_data, len);
            if (PRINT_VERBOSE) printk("[CS][INIT] buffered: local_steps=%d/%d\n", latest_local_steps.len, latest_local_steps.size);
		} else {
			printk("Not enough memory to store step data. (%d > %d)\n",
				latest_local_steps.len + result->step_data_buf->len,
				latest_local_steps.size);
			net_buf_simple_reset(&latest_local_steps);
			counter.dropped_ranging = result->header.procedure_counter;
			return;
		}
	}

	counter.dropped_ranging = PROCEDURE_COUNTER_NONE;
	counter.n_ap = result->header.num_antenna_paths; 

	if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_ABORTED) {
		printk("Procedure %u aborted\n", result->header.procedure_counter);
		net_buf_simple_reset(&latest_local_steps);
		k_sem_give(&sem_cs.procedure_done); 
		k_sem_give(&sem_cs.local_steps);
	}

	if (result->header.procedure_done_status == BT_HCI_LE_CS_PROCEDURE_DONE_STATUS_COMPLETE) {
		counter.most_recent_local_ranging = result->header.procedure_counter; 
		k_sem_give(&sem_cs.procedure_done); 
	}
}

void ranging_data_ready_cb(struct bt_conn *conn, uint16_t ranging_counter) {
    if (PRINT_VERBOSE) printk("[CS][INIT] RD ready: pc=%u conn=%d\n", ranging_counter, bt_conn_index(conn));
    counter.most_recent_peer_ranging = ranging_counter; 
	k_sem_give(&sem_cs.rd_ready); 
}

void ranging_data_get_complete_cb(struct bt_conn *conn, uint16_t ranging_counter, int err) {
	counter.ranging_data_err = err; 

	if (err) {
		printk("Error when getting ranging data with ranging counter %d (err %d)\n",
			ranging_counter, err);
	}

    if (!err) { if (PRINT_VERBOSE) printk("[CS][INIT] RD complete: pc=%u len=%d\n", ranging_counter, latest_peer_steps.len); }
	k_sem_give(&sem_cs.rd_complete); 
}

void ranging_data_overwritten_cb(struct bt_conn *conn, uint16_t ranging_counter) {
	printk("Ranging data overwritten %i\n", ranging_counter);
}
#endif 

/**********************************************************************/
/*                             GENERAL                                */
/**********************************************************************/

void cs_setup_struct_and_types() {
	#if BUILD_REFLECTOR 
	for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		memset(&ctx[i].counter, 0, sizeof(struct cs_count_ctx)); 
		k_sem_init(&ctx[i].sem_cs.cs_security, 0, 1); 
		k_sem_init(&ctx[i].sem_cs.rrq_capabilites, 0, 1); 
		k_sem_init(&ctx[i].sem_cs.create_config, 0, 1); 
	}
	#else 
	memset(&counter, 0, sizeof(struct cs_count_ctx)); 
	k_sem_init(&sem_cs.cs_security, 0, 1); 
	k_sem_init(&sem_cs.rrq_capabilites, 0, 1); 
	k_sem_init(&sem_cs.create_config, 0, 1); 
	k_sem_init(&sem_cs.local_steps, 0, 1); 
	k_sem_init(&sem_cs.procedure_disabled, 0, 1); 
	k_sem_init(&sem_cs.rd_complete, 0, 1); 
	k_sem_init(&sem_cs.rd_ready, 0, 1); 
	k_sem_init(&sem_cs.procedure_done, 0, 1); 
	#endif 
}

#if BUILD_REFLECTOR
void cs_reset_state(struct bt_conn *conn) {
    if (!conn) {
        printk("Error: NULL connection in cs_reset_state\n");
        return;
    }
    
    int idx = bt_conn_index(conn);
    if (idx < 0 || idx >= CONFIG_BT_MAX_CONN) {
        printk("Error: Invalid connection index %d in cs_reset_state\n", idx);
        return;
    }
    
    // Reset the connection-specific CS context
    memset(&ctx[idx].counter, 0, sizeof(struct cs_count_ctx));
    k_sem_reset(&ctx[idx].sem_cs.cs_security);
    k_sem_reset(&ctx[idx].sem_cs.rrq_capabilites);
    k_sem_reset(&ctx[idx].sem_cs.create_config);
    
    printk("CS state reset for connection %d\n", idx);
}
#endif 

#if BUILD_INITIATOR 
void cs_reset_state() {
	counter.most_recent_local_ranging = PROCEDURE_COUNTER_NONE;
    counter.dropped_ranging = PROCEDURE_COUNTER_NONE;
	counter.latest_frequency_compensation = 0;
	net_buf_simple_reset(&latest_local_steps);
    net_buf_simple_reset(&latest_peer_steps);
	k_sem_reset(&sem_cs.local_steps);
	k_sem_reset(&sem_cs.rd_ready);
	k_sem_reset(&sem_cs.rd_complete);
	k_sem_reset(&sem_cs.procedure_done); 
}

int cs_rreq_setup(struct bt_conn *conn) {
	TRY(bt_le_cs_set_default_settings(conn , &default_settings));

    TRY(bt_ras_rreq_rd_overwritten_subscribe(conn, ranging_data_overwritten_cb));

    TRY(bt_ras_rreq_rd_ready_subscribe(conn, ranging_data_ready_cb));

    TRY(bt_ras_rreq_on_demand_rd_subscribe(conn));

    TRY(bt_ras_rreq_cp_subscribe(conn));

	TRY(bt_le_cs_read_remote_supported_capabilities(conn));  

	k_sem_take(&sem_cs.rrq_capabilites, K_FOREVER); 

	return 0; 
}
#endif 

int cs_init(struct bt_conn *conn) {
    printk("Initializing Channel Sounding...\n");

	#if BUILD_INITIATOR 
	TRY(cs_rreq_setup(conn)); 
	#endif 

	TRY(cs_procedure_configure(conn)); 

    printk("CS initialized successfully\n");
    return 0;
}

int cs_procedure_configure(struct bt_conn *conn) {
	#if BUILD_INITIATOR 
    bt_le_cs_set_valid_chmap_bits(config_params.channel_map);

	TRY(bt_le_cs_create_config(conn, &config_params,
				     BT_LE_CS_CREATE_CONFIG_CONTEXT_LOCAL_AND_REMOTE));

	k_sem_take(&sem_cs.create_config, K_FOREVER); 
	k_sem_take(&sem_cs.cs_security, K_FOREVER); 

	TRY(bt_le_cs_set_procedure_parameters(conn, &procedure_params));
	#endif 
	return 0; 
}

#if BUILD_REFLECTOR
void configure_cs_connection(struct bt_conn *conn) {
	int err = 0; 
	int idx = bt_conn_index(conn);

    if (PRINT_VERBOSE) printk("[CS][REFL] configure: conn=%d default settings\n", bt_conn_index(conn));
	memset(&ctx[idx].counter, 0, sizeof(struct cs_count_ctx)); 
	k_sem_reset(&ctx[idx].sem_cs.cs_security);
    k_sem_reset(&ctx[idx].sem_cs.rrq_capabilites);
    k_sem_reset(&ctx[idx].sem_cs.create_config);

	TRY(bt_le_cs_set_default_settings(conn, &default_settings)); 
}
#endif 


#if BUILD_INITIATOR
int cs_start_ranging(struct bt_conn *conn) {
	net_buf_simple_reset(&latest_local_steps);
    net_buf_simple_reset(&latest_peer_steps);
    counter.dropped_ranging = PROCEDURE_COUNTER_NONE;
	#if USE_PSEUDO
    saved_local_steps_len = 0;
    saved_peer_steps_len = 0;
	#endif

	struct bt_le_cs_procedure_enable_param enable = { .config_id = ID, .enable = true };
    if (PRINT_VERBOSE) printk("[CS][INIT] start: conn=%d config_id=%d\n", bt_conn_index(conn), ID);
    return bt_le_cs_procedure_enable(conn, &enable);
}

int cs_stop_ranging(struct bt_conn *conn) {
	struct bt_le_cs_procedure_enable_param disable = { .config_id = ID, .enable = false };
	int err = bt_le_cs_procedure_enable(conn, &disable);

	if (err) {
		printk("Failed to disable CS procedure (err %d)\n", err);
		return err;
	}
	
	return err;
}

float cs_calc(struct bt_conn *conn) {
    float distance = 0.0f; 
    int32_t total_start_ms = 0;
    int32_t t0 = 0, t1 = 0;
    if (PRINT_TIME) total_start_ms = k_uptime_get();
    int err;
	// extern int sync_signal_completion(write_func_t write_fn);

	// 1. Wait for the local CS procedure to complete
    if (PRINT_TIME) t0 = k_uptime_get();
    err = k_sem_take(&sem_cs.procedure_done, K_MSEC(500));
    if (PRINT_TIME) { t1 = k_uptime_get(); printk("[CS][INIT][TIME] wait_procedure_done=%lld ms\n", (long long)(t1 - t0)); }
	if (err) {
		printk("Timeout waiting for local procedure done (err %d)\n", err); 
		return err;  
	}
	// TRY(sync_signal_completion(ble_write));
	

	// 2. Wait for the peer ranging data to be ready
    if (PRINT_TIME) t0 = k_uptime_get();
    err = k_sem_take(&sem_cs.rd_ready, K_MSEC(500));
    if (PRINT_TIME) { t1 = k_uptime_get(); printk("[CS][INIT][TIME] wait_rd_ready=%lld ms\n", (long long)(t1 - t0)); }
	if (err) {
		printk("Timeout waiting for ranging data ready (err %d)\n", err);
		return err;
	}

	// 3. Check counter match
	if (counter.most_recent_peer_ranging != counter.most_recent_local_ranging) {
		printk("Mismatch of local and peer ranging counters (%d != %d)\n",
				counter.most_recent_peer_ranging,
				counter.most_recent_local_ranging);
		return -EIO;
	}

    counter.ranging_data_err = 0;
    if (PRINT_TIME) t0 = k_uptime_get();
    err = bt_ras_rreq_cp_get_ranging_data(conn, &latest_peer_steps,
            counter.most_recent_peer_ranging, ranging_data_get_complete_cb); 
    if (err) {
        printk("Failed to start getting ranging data (err %d)\n", err);
        return err;
    }

    err = k_sem_take(&sem_cs.rd_complete, K_MSEC(500));
    if (PRINT_TIME) { t1 = k_uptime_get(); printk("[CS][INIT][TIME] get_and_wait_rd_complete=%lld ms\n", (long long)(t1 - t0)); }
    if (err) {
        printk("Timeout waiting for ranging data completion (err %d)\n", err);
        return err;
    }

    if (counter.ranging_data_err) {
        return counter.ranging_data_err;
    }

    const int tag_idx = bt_conn_index(conn);
    if (PRINT_TIME) t0 = k_uptime_get();
    
	#if USE_PSEUDO
    if (latest_local_steps.len <= sizeof(saved_local_steps_mem)) {
        memcpy(saved_local_steps_mem, latest_local_steps.data, latest_local_steps.len);
        saved_local_steps_len = latest_local_steps.len;
    } else {
        saved_local_steps_len = 0;
    }
    if (latest_peer_steps.len <= sizeof(saved_peer_steps_mem)) {
        memcpy(saved_peer_steps_mem, latest_peer_steps.data, latest_peer_steps.len);
        saved_peer_steps_len = latest_peer_steps.len;
    } else {
        saved_peer_steps_len = 0;
    }
	#endif

    distance = estimate_distance(&latest_local_steps, &latest_peer_steps, counter.n_ap,
                  BT_CONN_LE_CS_ROLE_INITIATOR, counter.latest_frequency_compensation, tag_idx);
	
	return distance;
}

#if USE_PSEUDO
float cs_calc_pseudo(struct bt_conn *conn) {
    float distance = 0.0f;
    const int tag_idx = bt_conn_index(conn);
    /* Restore snapshots into working buffers so the parser sees valid data */
    if (saved_local_steps_len == 0 || saved_peer_steps_len == 0) {
        return 0.0f;
    }

    net_buf_simple_reset(&latest_local_steps);
    net_buf_simple_reset(&latest_peer_steps);

    if (saved_local_steps_len <= latest_local_steps.size &&
        saved_peer_steps_len <= latest_peer_steps.size) {
        net_buf_simple_add_mem(&latest_local_steps, saved_local_steps_mem, saved_local_steps_len);
        net_buf_simple_add_mem(&latest_peer_steps, saved_peer_steps_mem, saved_peer_steps_len);
    } else {
        return 0.0f;
    }

    distance = estimate_distance(&latest_local_steps, &latest_peer_steps, counter.n_ap,
                                 BT_CONN_LE_CS_ROLE_INITIATOR, counter.latest_frequency_compensation, tag_idx);

    return distance;
}
#endif

int cs_wait_disabled() {
	return k_sem_take(&sem_cs.procedure_disabled, K_FOREVER); 
}
#endif 