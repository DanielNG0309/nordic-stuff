#pragma once 

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/bluetooth/cs.h>
#include "../global.h"

#define USER_LED DK_LED4
#define PRIO 5
#define STACK_SIZE 1024


/**********************************************************************/
/*                       STRUCTURES AND TYPES                         */
/**********************************************************************/

/** @brief Higher-order function used to update the led during indication */
typedef void (*led_cb_t)(const bool led_state); 

/** @brief higher-order function used for requesting writing */
typedef int (*write_func_t)(bool state); 

/** @brief higher-order function used for configuring CS */
typedef void (*configure_func_t)(struct bt_conn *conn); 

/** @brief higher-order function used for configuring CS */
typedef void (*indicate_func_t)(struct bt_conn *conn, uint8_t val); 

/** @brief Contains context used to control multiple initiators */
struct sync_handler {
    led_cb_t led_cb;
    atomic_t write_busy; 
    uint8_t att_err; 
    struct k_sem sem_write_done; 
    struct k_sem sem_reflector_ack; 
    struct k_sem sem_sync_init_done;
}; 

/** @brief Contains info about the current active CS initator */
/* --- REMOVED: FIFO container definitions are no longer needed. ---
struct fifo_container { ... };
#define FIFO_CONTAINER_DEFINE(...) ...
*/
    
/**********************************************************************/
/*                              GENERAL                               */
/**********************************************************************/
ssize_t sync_write_id(struct bt_conn *conn, const struct bt_gatt_attr *attr, 
	const void *buf, uint16_t len, uint16_t offset, uint8_t flags); 
int sync_init(struct sync_handler *callback);
// int sync_request_cs(write_func_t write_fn, bool state); // Keep for now, but unused in new flow
#if BUILD_INITIATOR
int sync_signal_completion(write_func_t write_fn); // NEW
#endif
void sync_update_led(bool val); 
bool sync_reflector_busy(); 

/**********************************************************************/
/*                              THREADS                               */
/**********************************************************************/
/* --- REMOVED: Replaced by orchestration thread in ble.c ---
void scheduling_thread();
void sync_put_fifo(struct fifo_container *container);
*/ 

/**********************************************************************/
/*                            CALLBACKS                               */
/**********************************************************************/

void sync_write_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params);
uint8_t sync_id_indicated(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, 
    const void *data, uint16_t length);
void sync_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value); 
void sync_reflector_ack_cb(bool state); 