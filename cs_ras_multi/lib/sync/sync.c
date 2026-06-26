#include "sync.h"

struct sync_handler sync_ctx;

int sync_init(struct sync_handler *callback) {
    if (callback) {
        sync_ctx.led_cb = callback->led_cb; 
    }
    sync_ctx.write_busy = 0; 
    sync_ctx.att_err = 0; 
    k_sem_init(&sync_ctx.sem_reflector_ack, 0, 1);
    k_sem_init(&sync_ctx.sem_write_done, 0, 1);

    k_sem_init(&sync_ctx.sem_sync_init_done, 0, 1); 
    k_sem_give(&sync_ctx.sem_sync_init_done);
    return 0; 
}

void sync_update_led(bool val) {
    if (sync_ctx.led_cb)    sync_ctx.led_cb(val); 
}

#if BUILD_INITIATOR
int sync_signal_completion(write_func_t write_fn) {
    int err;
    for (int attempt = 0; attempt < 5; attempt++) {
        err = write_fn(false);

        if (err == -EBUSY) {
            k_msleep(10);
            continue;
        }

        if (err) {
            printk("Write failed (err %d)\n", err);
            return err;
        }

        // Wait for the GATT write callback. Bounded, NOT K_FOREVER: under GATT
        // congestion across multiple connections the write callback can be delayed
        // by seconds, which previously parked the acquisition thread (the multi-second
        // "stall" with no DIST and no error). On timeout, report failure and let the
        // caller move on rather than hang.
        if (k_sem_take(&sync_ctx.sem_write_done, K_SECONDS(1)) != 0) {
            printk("Completion write callback timed out\n");
            return -ETIMEDOUT;
        }

        if (sync_ctx.att_err) {
            printk("Completion write failed (ATT 0x%02x)\n", sync_ctx.att_err);
            if (sync_ctx.att_err == BT_ATT_ERR_UNLIKELY || sync_ctx.att_err == BT_ATT_ERR_PREPARE_QUEUE_FULL) {
                 return -EIO;
            }
            k_msleep(10);
            continue;
        }
        return 0;
    }

    return -ETIMEDOUT;
}
#endif 

void sync_reflector_ack_cb(bool state) {
    dk_set_led(USER_LED, state); 
    k_sem_give(&sync_ctx.sem_reflector_ack); 
}

bool sync_reflector_busy() {
    return !atomic_cas(&sync_ctx.write_busy, 0, 1); 
}

/**********************************************************************/
/*                            CALLBACKS                               */
/**********************************************************************/

void sync_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    if (value == BT_GATT_CCC_INDICATE) {
        // printk("SYNC_ID indications enabled by client.\n");
    } else if (value == BT_GATT_CCC_NOTIFY) {
        // printk("SYNC_ID notifications enabled by client.\n");
    } else if (value == 0) {
        // printk("SYNC_ID notifications/indications disabled by client.\n");
    } else {
        printk("SYNC_ID CCCD changed to unknown value: 0x%04x\n", value);
    }
}

void sync_write_cb(struct bt_conn *conn, uint8_t err,
                    struct bt_gatt_write_params *params) {
    sync_ctx.write_busy = 0; 
    sync_ctx.att_err = err; 
    k_sem_give(&sync_ctx.sem_write_done);

    if (err != 0x00 && err != BT_ATT_ERR_PREPARE_QUEUE_FULL) {
        printk("SYNC_ID write failed  (ATT 0x%02x)\n", err); 
    }
}

uint8_t sync_id_indicated(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, 
    const void *data, uint16_t length) {
    if (data == NULL) {
        printk("Unsubscribed from SYNC_ID indications\n");
        params->value_handle = 0;
        return BT_GATT_ITER_STOP;
    }

    if (length != sizeof(uint8_t)) {
        printk("Received SYNC_ID indication with invalid length: %d\n", length);
        return BT_GATT_ITER_CONTINUE;
    }

    uint8_t received_value = *((uint8_t *)data);

    // Update local state
    if (sync_ctx.led_cb) {
        // Call callback in initiator.c to signal acquisition_thread()
        sync_ctx.led_cb(received_value ? true : false);
    }
    
    return BT_GATT_ITER_CONTINUE;
}