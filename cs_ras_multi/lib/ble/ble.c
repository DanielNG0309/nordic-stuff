#include "ble.h"

struct ble_ctx ble_info; 
struct semaphores_ctx semaphores; 
struct ble_state_handler ble_state; 
extern atomic_t write_busy; 


#if BUILD_REFLECTOR
// Synchronization context for orchestration
struct reflector_sync_ctx {
    struct bt_conn *active_conn;
    int turn_idx;
    struct k_sem indication_complete; // Semaphore for indication completion
    struct k_sem ranging_complete;    // Semaphore to signal orchestration thread that ranging finished
} reflector_sync = {0};

// Define stack and thread data for the new thread
K_THREAD_STACK_DEFINE(orchestration_stack, 2048);
struct k_thread orchestration_thread_data;

// Forward declarations
void orchestration_thread(void *p1, void *p2, void *p3);
int ble_indicate_and_wait(struct bt_conn *conn, uint8_t val); 

static const struct bt_le_conn_param ble_param = {
    .interval_min = 15, 
    .interval_max = 15, 
    .latency = 0, 
    .timeout = 400,
}; 

const static struct bt_le_scan_param search_param = {
    .type = BT_HCI_LE_SCAN_ACTIVE, 
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = 20,
    .window = 6
};

static struct bt_scan_init_param scan_param = {
    .connect_if_match = true, 
    .scan_param = &search_param, 
    .conn_param = &ble_param,
};

#endif 

#if BUILD_INITIATOR
struct indicate_handler indicate_ctx;

static struct bt_gatt_dm_cb gatt_callbacks = {
    .completed = dm_completed, 
    .service_not_found = dm_service_not_found, 
    .error_found = dm_error_found, 
};



static struct bt_gatt_exchange_params mtu_exchange_params = {.func = mtu_exchange_cb};

static const struct bt_data adv[] = {
  BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
  BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                BT_UUID_16_ENCODE(BT_UUID_RANGING_SERVICE_VAL)),
  BT_DATA_BYTES(BT_DATA_UUID128_ALL, SYNC_SERVICE_UUID_VAL),
};

static const struct bt_data scan_rsp[] = {
  BT_DATA(BT_DATA_NAME_COMPLETE, INITIATOR_NAME,
          sizeof(INITIATOR_NAME) - 1),
};
#endif 

/**********************************************************************/
/*                           CALLBACKS                                */
/**********************************************************************/

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
    .le_phy_updated = phy_changed_cb, 
};


void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];

    (void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Connected to %s (err %d)\n", addr, err);

    if (err) {
        printk("Failed to connect to %s (err %d)\n", addr, err);

        #if BUILD_REFLECTOR 
        for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
            if (ble_info.connections[i] == conn) {
                bt_conn_unref(ble_info.connections[i]);
                ble_info.connections[i] = NULL;
                ble_info.conn_count--;
                printk("Connection slot %d freed\n", i);
                break;
            }
        }
        #endif 

        #if BUILD_INITIATOR 
        bt_conn_unref(conn);
        conn = NULL; 
        ble_info.connection = NULL; 
        #endif 
        return; 
    }

    #if BUILD_REFLECTOR 
    for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
        if (ble_info.connections[i] == NULL) {
            ble_info.connections[i] = bt_conn_ref(conn);
            ble_info.conn_count++;
            printk("Connection slot %d assigned\n", i);

            TRY(bt_conn_le_param_update(conn, &ble_param));
            ble_set_phy(conn);
            configure_cs_connection(conn);
            dk_set_led_on(DK_LED1 + i);
            break;
        }
    }

    if (ble_info.conn_count == CONFIG_BT_MAX_CONN) {
        TRY(bt_le_scan_stop()); 
        ble_update_state(INACTIVE_SCAN); 
    } else {
        TRY(bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE)); // TODO: Better check to stop starting in cases where its still on
        ble_update_state(ACTIVE_SCAN); 
    }
    #endif 

    #if BUILD_INITIATOR 
    ble_info.connection = bt_conn_ref(conn); 
    dk_set_led_on(DK_LED1);

    TRY(bt_le_adv_stop());
    ble_update_state(INACTIVE_ADV);
    #endif 
 
    k_sem_give(&semaphores.connected);
}

void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];

    (void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Disconnected from %s (reason 0x%02x)\n", addr, reason);

    #if BUILD_REFLECTOR

    // If the disconnected connection was the active one, unblock the orchestration thread.
    if (conn == reflector_sync.active_conn) {
        reflector_sync.active_conn = NULL;
        k_sem_give(&reflector_sync.ranging_complete);
    }

    for(int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
        if (ble_info.connections[i] == conn) {
            bt_conn_unref(ble_info.connections[i]);
            ble_info.connections[i] = NULL;
            dk_set_led_off(DK_LED1 + i); 
            ble_info.conn_count--;
            printk("Connection slot %d freed\n", i);
            break;
        }
    }

    if (ble_info.conn_count < CONFIG_BT_MAX_CONN) {
        TRY(bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE));
        ble_update_state(ACTIVE_SCAN); 
    } 
    #endif 

    #if BUILD_INITIATOR 
    bt_conn_unref(conn);
    ble_info.connection = NULL; 

    cs_reset_state(); 
    TRY(ble_adv_start());
    ble_update_state(ACTIVE_ADV); 
    #endif
}

void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];

    (void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Security changed for %s to level %d (err %d)\n", addr, level, err);

    if (err) {
        printk("Failed to change security for %s (err %d)\n", addr, err);
        return;
    } 

    k_sem_give(&semaphores.security);
}

#if BUILD_INITIATOR && CONFIG_BT_CHANNEL_SOUNDING
void dm_completed(struct bt_gatt_dm *dm, void *context) {
    int err;

    printk("The discovery procedure succeeded\n");

    struct bt_conn *conn = bt_gatt_dm_conn_get(dm);

    bt_gatt_dm_data_print(dm);

    // Subscribing to SYNC_SERIVICE
    if (context != NULL) {
        static struct bt_uuid_128 sync_id_uuid_inst = BT_UUID_INIT_128(SYNC_ID_UUID_VAL);
        
        const struct bt_gatt_dm_attr *sync_id_chrc_attr = bt_gatt_dm_char_by_uuid(dm, &sync_id_uuid_inst.uuid);
        if (!sync_id_chrc_attr) {
            printk("SYNC_ID_UUID characteristic not found!\n");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            goto release_dm_data;
        }
        indicate_ctx.char_handle = sync_id_chrc_attr->handle + 1;

        const struct bt_gatt_dm_attr *sync_id_ccc_attr = bt_gatt_dm_desc_by_uuid(dm, sync_id_chrc_attr, BT_UUID_GATT_CCC);
        if (!sync_id_ccc_attr) {
            printk("CCCD for SYNC_ID_UUID not found!\n");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            goto release_dm_data;
        }
        indicate_ctx.ccc_handle = sync_id_ccc_attr->handle;
        
        // Set up subscription parameters
        indicate_ctx.sub_params.ccc_handle = indicate_ctx.ccc_handle;
        indicate_ctx.sub_params.value_handle = indicate_ctx.char_handle;
        indicate_ctx.sub_params.value = BT_GATT_CCC_INDICATE;
        indicate_ctx.sub_params.notify = sync_id_indicated;
        indicate_ctx.sub_params.subscribe = NULL; 

        err = bt_gatt_subscribe(conn, &indicate_ctx.sub_params);
        if (err && err != -EALREADY) {
            printk("Failed to subscribe to SYNC_ID_UUID (err %d)\n", err);
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            goto release_dm_data;
        }

        printk("Successfully subscribed to SYNC_ID indications\n");

    } else {
        // This is the ranging service discovery
        TRY(bt_ras_rreq_alloc_and_assign_handles(dm, conn));
    } 

release_dm_data:
    bt_gatt_dm_data_release(dm);
    k_sem_give(&semaphores.discovery);
}


void dm_service_not_found(struct bt_conn *conn, void *context) {
    printk("The service could not be found during the discovery, disconnecting\n");
    bt_conn_disconnect(ble_info.connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

void dm_error_found(struct bt_conn *conn, int err, void *context) {
    printk("The discovery procedure failed (err %d)\n", err);
    bt_conn_disconnect(ble_info.connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params) {
	if (err) {
		printk("MTU exchange failed (err %d)\n", err);
		return;
	}

	printk("MTU exchange success (%u)\n", bt_gatt_get_mtu(conn));
    k_sem_give(&semaphores.mtu_exchange);
}
#endif 

void phy_changed_cb(struct bt_conn *conn, struct bt_conn_le_phy_info *param) {
    switch (param->tx_phy) {
        case BT_CONN_LE_TX_POWER_PHY_1M: 
            printk("Phy updated to 1M\n");
            break; 
        
        case BT_CONN_LE_TX_POWER_PHY_2M: 
            printk("Phy updated to 2M\n");
            break; 

        default: break; 
    }
}

/**********************************************************************/
/*                        SERVER / CLIENT                             */
/**********************************************************************/

BT_GATT_SERVICE_DEFINE(ble_sync_service,
    BT_GATT_PRIMARY_SERVICE(SYNC_SERVICE_UUID),
    BT_GATT_CHARACTERISTIC(SYNC_ID_UUID, 
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE, 
        BT_GATT_PERM_WRITE_ENCRYPT, 
        NULL, sync_write_id, NULL),
    BT_GATT_CCC(sync_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT)
);

enum ble_state_ctx ble_get_currrent_state() {
    return ble_state.state; 
}

#if BUILD_INITIATOR 
int ble_write(bool state) {
    static struct bt_gatt_write_params write_params_led;
    struct bt_conn *c = ble_info.connection; 
    if (!c) {
        printk("LED write skipped - not connected\n");
        sys_reboot(SYS_REBOOT_COLD);
        return -ENOTCONN;
    }

    if (indicate_ctx.char_handle == 0x0000) {
        printk("Invalid characteristic handle. Discovery might have failed.\n");
        return -EINVAL;
    }

    static uint8_t state_val; 
    state_val = state ? 0x01 : 0x00;

    write_params_led.func = sync_write_cb; 
    write_params_led.handle = indicate_ctx.char_handle;
    write_params_led.offset = 0;
    write_params_led.data = &state_val;
    write_params_led.length = sizeof(state_val);

    return bt_gatt_write(c, &write_params_led);
}
#endif 

#if BUILD_REFLECTOR
// Updated indicate_done to signal the orchestration thread
void indicate_done(struct bt_conn *conn,
					struct bt_gatt_indicate_params *params,
					uint8_t err) {

    if (err) {
        printk("Indication failed (err %d)\n", err);
    }
    // Signal that the indication confirmation has been received (or failed).
    k_sem_give(&reflector_sync.indication_complete);
}
#endif 

// Rewritten sync_write_id to handle completion signal (Write 0x00) only.
ssize_t sync_write_id(struct bt_conn *conn, const struct bt_gatt_attr *attr,
	const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {

    // ... (Validation checks) ...
    uint8_t val = *((uint8_t *)buf);

    // Initiator writes 0x00 to signal completion.
    if(val != 0x00) {
        printk("Incorrect write value: %d. Expecting 0x00.\n", val);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    sync_update_led(false);

    #if BUILD_REFLECTOR

    // Check if this write comes from the expected active connection
    if (conn != reflector_sync.active_conn) {
        printk("Received completion signal from unexpected initiator.\n");

        if (reflector_sync.active_conn == NULL) {
            // Orchestration might have timed out already. Accept the write.
            return len;
        }
        // Reject if it's clearly the wrong state.
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    // Ranging is complete for this initiator.
    reflector_sync.active_conn = NULL;

    // Signal the orchestration thread to proceed to the next initiator.
    k_sem_give(&reflector_sync.ranging_complete);

    #endif

    return len;
}

#if BUILD_REFLECTOR

// Helper function to send indication and wait synchronously
int ble_indicate_and_wait(struct bt_conn *conn, uint8_t val) {
    static struct bt_gatt_indicate_params indicate_params;
    static uint8_t indicate_value;

    indicate_value = val;

    memset(&indicate_params, 0, sizeof(indicate_params));
    // Ensure the attribute pointer points to the SYNC_ID characteristic 
    indicate_params.attr = &ble_sync_service.attrs[1];
    indicate_params.data = &indicate_value;
    indicate_params.len = sizeof(indicate_value);
    indicate_params.func = indicate_done;

    int ret = bt_gatt_indicate(conn, &indicate_params);

    if (ret) {
        printk("Failed to send indication (err %d)\n", ret);
        return ret;
    }

    // Wait for the indication confirmation from the client
    ret = k_sem_take(&reflector_sync.indication_complete, K_SECONDS(1));
    if (ret) {
        printk("Timeout waiting for indication confirmation\n");
        return ret;
    }
    return 0;
}

// The orchestration thread implementation
void orchestration_thread(void *p1, void *p2, void *p3) {
    printk("Reflector Orchestration Thread Started.\n");

    while (true) {
        // Wait until there is at least one connection
        if (ble_info.conn_count == 0) {
            k_msleep(100);
            reflector_sync.turn_idx = 0; // Reset index when idle
            continue;
        }
        
        // Give initiator time to complete GATT discovery and subscription
        // This prevents sending indications before the client is ready
        // k_msleep(30);

        // 1. Find the next initiator (Round-Robin)
        struct bt_conn *next_conn = NULL;
        int start_idx = reflector_sync.turn_idx;

        // Ensure index is within bounds
        if (start_idx >= CONFIG_BT_MAX_CONN) {
            start_idx = 0;
        }

        // Iterate through the connection slots to find the next active connection
        for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
            int idx = (start_idx + i) % CONFIG_BT_MAX_CONN;
            if (ble_info.connections[idx] != NULL) {
                next_conn = ble_info.connections[idx];
                reflector_sync.turn_idx = idx;
                break;
            }
        }

        if (!next_conn) {
            k_msleep(15);
            continue;
        }

        // 2. Signal the initiator (Indicate SYNC_ID = 1: Start Ranging)
        reflector_sync.active_conn = next_conn;
        k_sem_reset(&reflector_sync.ranging_complete); // Reset semaphore before starting

        printk("Signaling initiator %d to start ranging...\n", reflector_sync.turn_idx);
        int err = ble_indicate_and_wait(next_conn, 0x01);

        if (err) {
            printk("Failed to signal initiator %d (err %d). Retrying in 100ms.\n", reflector_sync.turn_idx, err);
            // If signaling fails, clear active state and wait before retrying
            reflector_sync.active_conn = NULL;
            k_msleep(20); // Brief pause before retrying
            continue; // Retry with same initiator
        }

        // 3. Wait for the initiator to complete ranging (signaled by sync_write_id).
        // The initiator now signals completion AFTER computing distance, so this is a
        // real measurement-completion wait. Timeout must cover worst-case cs_calc
        // (~1.5s of bounded sem waits) + the completion write (~1s). On timeout we log
        // (real stall detection) and advance so one bad initiator can't wedge the loop.
        err = k_sem_take(&reflector_sync.ranging_complete, K_SECONDS(3));

        if (err) {
            printk("Timeout waiting for initiator %d to complete ranging.\n", reflector_sync.turn_idx);
        }

        // 4. Advance the turn
        if (reflector_sync.active_conn != NULL) {
            // Ensure active_conn is cleared if timeout occurred or semaphore was released by disconnect.
            reflector_sync.active_conn = NULL;
        }

        reflector_sync.turn_idx = (reflector_sync.turn_idx + 1) % CONFIG_BT_MAX_CONN;

        k_yield(); // Allow other threads (like BLE stack) to run
    }
}
#endif 

/**********************************************************************/
/*                             GENERAL                                */
/**********************************************************************/

struct bt_conn *ble_init() {
    ble_setup_struct_and_types(); 
    cs_setup_struct_and_types();

    dk_leds_init();

    TRY(bt_enable(NULL)); 

    #if BUILD_INITIATOR 
        TRY(ble_adv_start());

        printk("Initator started, advertising as %s\n", INITIATOR_NAME);

        ble_update_state(ACTIVE_ADV); 

        k_sem_take(&semaphores.connected, K_FOREVER); // wait for connection

        TRY(bt_conn_set_security(ble_info.connection, BT_SECURITY_L2));

        k_sem_take(&semaphores.security, K_FOREVER); // wait for security to be established

        bt_gatt_exchange_mtu(ble_info.connection, &mtu_exchange_params);

        k_sem_take(&semaphores.mtu_exchange, K_FOREVER); // wait for maximum transition unit (MTU) exchange

        TRY(bt_gatt_dm_start(ble_info.connection, BT_UUID_RANGING_SERVICE, &gatt_callbacks, NULL));
        
        k_sem_take(&semaphores.discovery, K_FOREVER);
        
        TRY(bt_gatt_dm_start(ble_info.connection, SYNC_SERVICE_UUID, &gatt_callbacks, &indicate_ctx.char_handle));

        k_sem_take(&semaphores.discovery, K_FOREVER);
   
        printk("GATT discovery started successfully\n");

        return ble_info.connection; 
    #endif 

    #if BUILD_REFLECTOR
        TRY(ble_scan_init()); 
        
        TRY(bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE));

        ble_update_state(ACTIVE_ADV); 

        return NULL; 
    #endif 
}

void ble_update_state(enum ble_state_ctx state) {
    k_mutex_lock(&ble_state.mutex, K_FOREVER); 
    ble_state.state = state; 
    k_mutex_unlock(&ble_state.mutex); 
}

#if BUILD_REFLECTOR 
int ble_scan_init() {
    bt_scan_init(&scan_param);
    TRY(bt_scan_filter_add(BT_SCAN_FILTER_TYPE_NAME, INITIATOR_NAME));
    TRY(bt_scan_filter_enable(BT_SCAN_NAME_FILTER, true));
    printk("BLE init done, scanning for %s\n", INITIATOR_NAME);
    return 0;
}
#endif 

int ble_setup_struct_and_types() {
    k_sem_init(&semaphores.connected, 0, 1); 
    k_sem_init(&semaphores.security, 0, 1); 

    #if BUILD_REFLECTOR 
    memset(ble_info.connections, 0, sizeof(ble_info.connections)); 
    ble_info.conn_count = 0; 
    // NEW: Initialize orchestration context and start the thread
    k_sem_init(&reflector_sync.indication_complete, 0, 1);
    k_sem_init(&reflector_sync.ranging_complete, 0, 1);

    k_thread_create(&orchestration_thread_data, orchestration_stack,
                    K_THREAD_STACK_SIZEOF(orchestration_stack),
                    orchestration_thread,
                    NULL, NULL, NULL,
                    5, 0, K_NO_WAIT); // Priority 5
    #endif 

    #if BUILD_INITIATOR 
    ble_info.connection = NULL; 
    k_sem_init(&semaphores.mtu_exchange, 0, 1); 
    k_sem_init(&semaphores.discovery, 0, 1); 
    #endif 

    k_mutex_init(&ble_state.mutex);
    ble_state.state = SETUP; 

    return 0; 
}

void ble_set_phy(struct bt_conn *conn) {
    const struct bt_conn_le_phy_param phy = {
       .options = BT_CONN_LE_PHY_OPT_NONE,
       .pref_tx_phy = BT_GAP_LE_PHY_2M, 
       .pref_rx_phy = BT_GAP_LE_PHY_2M,
    };

    TRY(bt_conn_le_phy_update(conn, &phy)); 
}

#if BUILD_INITIATOR 
int ble_adv_start() {
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, adv, ARRAY_SIZE(adv), scan_rsp, ARRAY_SIZE(scan_rsp)); 
}

#endif 