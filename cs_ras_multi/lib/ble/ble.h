#pragma once

#include "../global.h"
#include <cs.h>
#include <sync.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/reboot.h>
#include <dk_buttons_and_leds.h>

#if BUILD_REFLECTOR 
#include <bluetooth/scan.h>
#endif 

#include <zephyr/bluetooth/gap.h>
#include <zephyr/types.h>

struct bt_conn *ble_init(); 
int ble_scan_init();
int ble_adv_start(); 
void ble_set_phy(struct bt_conn *conn); 
void ble_connections_handler(); 
int ble_write(bool led_state); 


/**********************************************************************/
/*                       STRUCTURES AND TYPES                         */
/**********************************************************************/

/** @brief  Contains the context of the device bluetooth state */
enum ble_state_ctx {
    SETUP, 
    ACTIVE_ADV, 
    ACTIVE_SCAN, 
    INACTIVE_ADV, 
    INACTIVE_SCAN, 
};

struct ble_state_handler {
    enum ble_state_ctx state; 
    struct k_mutex mutex; 
}; 

/** @brief Contains the context of bluetooth connection"s" */
struct ble_ctx {
    #if BUILD_REFLECTOR
    struct bt_conn *connections[CONFIG_BT_MAX_CONN]; 
    uint8_t conn_count; 
    #endif 

    #if BUILD_INITIATOR 
    struct bt_conn *connection; 
    #endif 
};

/** @brief Contains the context of semaphores used for bluetooth stack */
struct semaphores_ctx {
    struct k_sem connected; 
    struct k_sem security; 

    #if BUILD_INITIATOR 
    struct k_sem mtu_exchange; 
    struct k_sem discovery; 
    #endif 
}; 

/** @brief Contians info about gatt writting and indication */
struct indicate_handler {
    struct bt_gatt_write_params write_led_param; 
    struct bt_gatt_subscribe_params sub_params; 
    uint16_t char_handle; 
    uint16_t ccc_handle; 
}; 


int ble_setup_struct_and_types(); 
void ble_update_state(enum ble_state_ctx state); 
enum ble_state_ctx ble_get_currrent_state(); 

/**********************************************************************/
/*                 NOTIFICATION / SYNCHRONIZATION                     */
/**********************************************************************/

/** @brief Service UUID for ble synchronization */ 
#define SYNC_SERVICE_UUID_VAL \
    BT_UUID_128_ENCODE(0xca03b72e, 0xd5dd, 0x438e, 0xbda5, 0x2d4e8780d565)

#define SYNC_SERVICE_UUID BT_UUID_DECLARE_128(SYNC_SERVICE_UUID_VAL)

/** @brief ID of initiator */ 
#define SYNC_ID_UUID_VAL \
    BT_UUID_128_ENCODE(0x6159b793, 0x24a3, 0x4402, 0xbd84, 0xd6375a4ce9b5)

#define SYNC_ID_UUID BT_UUID_DECLARE_128(SYNC_ID_UUID_VAL)

void indicate_done(struct bt_conn *conn,
					struct bt_gatt_indicate_params *params,
					uint8_t err); 

#if BUILD_REFLECTOR 
void ble_indicate_write(struct bt_conn *conn, uint8_t val); 
#endif 
    
/**********************************************************************/
/*                           CALLBACKS                                */
/**********************************************************************/

void connected(struct bt_conn *conn, uint8_t err);
void disconnected(struct bt_conn *conn, uint8_t reason);
void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err);
void dm_completed(struct bt_gatt_dm *dm, void *context);
void dm_service_not_found(struct bt_conn *conn, void *context);
void dm_error_found(struct bt_conn *conn, int err, void *context);
void mtu_exchange_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params);
void phy_changed_cb(struct bt_conn *conn, struct bt_conn_le_phy_info *param); 