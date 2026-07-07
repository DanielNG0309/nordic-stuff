#include <ble.h>
#include <sync.h> // Ensure sync.h is included

static void app_led_cb(bool led_state) {
	dk_set_led(USER_LED, led_state); 
  // TRY(bt_le_scan_stop());  // TODO: use semaphore or other thread safe method just in case
}

static struct sync_handler cb = {
  .led_cb = app_led_cb,
};

int main(void) {
  printk("Starting Nordic CS Reflector...\n");

  TRY_RETURN(sync_init(&cb));
  ble_init(); // Starts orchestration_thread()

  while (true) {
    k_msleep(100);
  }
  return 0;
}
