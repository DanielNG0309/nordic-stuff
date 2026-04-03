#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(flpr_sleep_test, LOG_LEVEL_INF);

/*
 * FLPR core image — shared by nRF54H20, nRF54LM20A and nRF54L15.
 *
 * Active phase: quadratic computation (ax² + bx + c) to simulate a
 * realistic RISC-V workload using MUL/ADD instructions.
 * Tune COMPUTE_ITERATIONS to achieve ~1-2 ms active time at the FLPR
 * clock frequency — verify the pulse width on a PPK trace.
 *
 * PM selects the sleep state based on TEST_SLEEP_DURATION_MS vs the
 * min-residency-us values defined in the board FLPR overlay:
 *   300 ms  -> WAIT        (substate 0, min-residency  20 ms)
 *   500 ms  -> SLEEP       (substate 1, min-residency 400 ms)
 *   800 ms  -> DEEP_SLEEP  (substate 2, min-residency 600 ms)
 *   1200 ms -> HIBERNATE   (substate 3, min-residency 1000 ms)
 *
 * Note: nRF54H20 FLPR has only two power states (wait / suspend-to-ram).
 *
 * Baseline mode (CONFIG_TEST_BASELINE_SLEEP=y):
 *   FLPR sleeps forever — no compute, no GRTC wakeup.  Used to measure
 *   the true system current floor.
 */



#define COMPUTE_ITERATIONS 65000
#define BUSY_WAIT_INTERVAL 5000

static const struct gpio_dt_spec boot_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
#if defined(CONFIG_TEST_BASELINE_SLEEP)
	while (1) {
		k_sleep(K_FOREVER);
	}
#else
	int counter = 0;

	/* Brief LED flash on boot so FLPR startup is visible on a PPK trace. */
	if (gpio_is_ready_dt(&boot_led)) {
		gpio_pin_configure_dt(&boot_led, GPIO_OUTPUT_INACTIVE);
		gpio_pin_set_dt(&boot_led, 1);
		k_busy_wait(5000); /* 5 ms */
		gpio_pin_set_dt(&boot_led, 0);
	}

	LOG_INF("Multicore flpr_sleep_test on %s", CONFIG_BOARD_TARGET);
	LOG_INF("Main sleeps for %d ms", CONFIG_TEST_SLEEP_DURATION_MS);

	while (1) {
		volatile uint32_t result = 0;

		for (uint32_t i = 0; i < COMPUTE_ITERATIONS; i++) {
			result += (i * i) + (3 * i) + 7;
		}

		LOG_INF("Run %d (result=%u)", counter, (unsigned int)result);
		counter++;
		/*
		k_busy_wait(BUSY_WAIT_INTERVAL);
		*/
		k_msleep(CONFIG_TEST_SLEEP_DURATION_MS);
	}
#endif /* CONFIG_TEST_BASELINE_SLEEP */

	return 0;
}