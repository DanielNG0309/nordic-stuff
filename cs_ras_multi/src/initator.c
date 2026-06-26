#include <ble.h>
#include <sync.h>

struct bt_conn *conn;
K_SEM_DEFINE(init_done_sem, 0, 1);
K_SEM_DEFINE(start_ranging_sem, 0, 1);

// Callback implementation to handle the start signal (Indication)
void initiator_sync_cb(bool start_signal) {
    dk_set_led(USER_LED, start_signal);

    if (start_signal) {
        // Signal the acquisition thread to start ranging
        k_sem_give(&start_ranging_sem);
    }
}

static struct sync_handler cb = {
    .led_cb = initiator_sync_cb,
};

void acquisition_thread() {
    float distance = 0;
    struct bt_conn *c = NULL;
    int32_t loop_start = 0, loop_end = 0;
    
    #if USE_PSEUDO
    int sample_count = 0; // Counter to know when to inject pseudo data
    #endif

	k_sem_take(&init_done_sem, K_FOREVER);

    c = conn; 
	if (!c) {
		printk("Connection is NULL even after init. Aborting.\n");
		return;
	}

    printk("Connection ready. Waiting for reflector signal...\n");

    while (true) {

        // Wait for reflector start signal
        k_sem_take(&start_ranging_sem, K_FOREVER);

        if (PRINT_TIME) loop_start = k_uptime_get();
        if(!c)  { k_msleep(50); continue;}

	    cs_reset_state();
        TRY(cs_start_ranging(c));
        distance = cs_calc(c);
        /* Signal completion AFTER computing, so the reflector advances on a real
         * measurement rather than merely "started" — this makes its ranging_complete
         * timeout an actual stall detector. Always signal (even on a failed/zero
         * measurement) so the reflector never blocks waiting on us. */
        TRY(sync_signal_completion(ble_write));

#if USE_PSEUDO
        if (sample_count > 10) {
            for (int i = 0; i < PSEUDO_INJECTIONS_COUNT; i++) {
                k_msleep(100);
                float pseudo_distance = cs_calc_pseudo(c);
            }
        } else {
            sample_count++;
        }
#endif

        if (PRINT_TIME) {
            loop_end = k_uptime_get();
            printk("[INIT][TIME] loop_total=%lld ms\n", (long long)(loop_end - loop_start));
        }
	}

}

int main(void) {
    printk("Starting Nordic CS Initiator...\n");

    TRY_RETURN(sync_init(&cb));

    conn = ble_init();
    TRY(cs_init(conn));

	printk("Setup complete. Handing over to acquisition thread.\n");
	k_sem_give(&init_done_sem);
	
	while (true) {
	    k_msleep(100);
	}
	
    return 0;
}

K_THREAD_DEFINE(
    acquisition, 16384, acquisition_thread, 
    NULL, NULL, NULL, 7, 0, 0
);