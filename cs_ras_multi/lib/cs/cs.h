#pragma once 

#include "../global.h"
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/conn.h>
#include <bluetooth/services/ras.h>

#if BUILD_INITIATOR
#include <calc.h>
#endif


int cs_init(struct bt_conn *conn); 
int cs_procedure_configure(struct bt_conn *conn);

#if BUILD_REFLECTOR
void configure_cs_connection(struct bt_conn *conn); 
void cs_reset_state(struct bt_conn *conn); 
#endif

#if BUILD_INITIATOR
void cs_reset_state(); 
int cs_rreq_setup(struct bt_conn *conn); 
float cs_calc(struct bt_conn *conn);
float cs_calc_pseudo(struct bt_conn *conn);
int cs_start_ranging(struct bt_conn *conn); 
int cs_stop_ranging(struct bt_conn *conn);
int cs_wait_disabled();
#endif

/**********************************************************************/
/*                       STRUCTURES AND TYPES                         */
/**********************************************************************/

/** @brief Contains sempahores used during channel sounding */
struct cs_semaphores_ctx {
    struct k_sem cs_security; 
    struct k_sem rrq_capabilites; 
    struct k_sem create_config; 

    #if BUILD_INITIATOR 
    struct k_sem local_steps; 
    struct k_sem procedure_disabled;
    struct k_sem rd_complete; 
    struct k_sem rd_ready; 
    struct k_sem procedure_done;
    #endif 
};

/** @brief Contains counting systems */
struct cs_count_ctx { 
    #if BUILD_INITIATOR
    int32_t most_recent_local_ranging;
    int32_t dropped_ranging;
    int ranging_data_err;

    /* Needed by cs_calc() for both the black-box and cs_de IFFT paths. */
    uint16_t latest_frequency_compensation;
    int32_t most_recent_peer_ranging;
    uint8_t n_ap;
    #endif
};

struct cs_ctx {
    struct cs_semaphores_ctx sem_cs; 
    struct cs_count_ctx counter; 
}; 

void cs_setup_struct_and_types(); 

/**********************************************************************/
/*                           CALLBACKS                                */
/**********************************************************************/

void remote_capabilities_cb(struct bt_conn *conn, uint8_t status, struct bt_conn_le_cs_capabilities *params); 
void security_enable_cb(struct bt_conn *conn, uint8_t status);
void config_create_cb(struct bt_conn *conn, uint8_t status, struct bt_conn_le_cs_config *config); 
void procedure_enable_cb(struct bt_conn *conn, uint8_t status, struct bt_conn_le_cs_procedure_enable_complete *params); 
void subevent_result_cb(struct bt_conn *conn, struct bt_conn_le_cs_subevent_result *result); 
void ranging_data_overwritten_cb(struct bt_conn *conn, uint16_t ranging_counter);
void ranging_data_ready_cb(struct bt_conn *conn, uint16_t ranging_counter); 
void ranging_data_get_complete_cb(struct bt_conn *conn, uint16_t ranging_counter, int err); 
void read_remote_fae_table_complete_cb(struct bt_conn *conn, uint8_t status, struct bt_conn_le_cs_fae_table *params);