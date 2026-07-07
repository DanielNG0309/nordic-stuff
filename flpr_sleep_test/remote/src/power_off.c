#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/devicetree.h>
#include <hal/nrf_vpr_csr.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(power_off, LOG_LEVEL_INF);

#define VIPER_SLEEP_SUBSTATE_WAIT 0

#if defined(CONFIG_SOC_NRF54H20_CPUFLPR)
/* nRF54H20 FLPR has two sleep states:
 *   0 = HIBERNATE  (PM_STATE_SUSPEND_TO_RAM)  context saved, core resets on wakeup
 *   1 = DEEPSLEEP  (PM_STATE_SUSPEND_TO_RAM)  noop
 */
#define VIPER_SLEEP_SUBSTATE_HIBERNATE 0
#define VIPER_SLEEP_SUBSTATE_DEEPSLEEP 1
#else
/* All other FLPR cores (nRF54LM20A, nRF54L15) have four sleep states:
 *   0 = WAIT       (PM_STATE_STANDBY)         clock gated, fast wakeup
 *   1 = SLEEP      (PM_STATE_SUSPEND_TO_RAM)  clock off, RAM on, resumable
 *   2 = DEEP_SLEEP (PM_STATE_SUSPEND_TO_RAM)  clock off, RAM retained, resumable
 *   3 = HIBERNATE  (PM_STATE_SUSPEND_TO_RAM)  power off, context saved, resets on wakeup
 */
#define VIPER_SLEEP_SUBSTATE_SLEEP      1
#define VIPER_SLEEP_SUBSTATE_DEEP_SLEEP 2
#define VIPER_SLEEP_SUBSTATE_HIBERNATE  3
#endif

static void pm_go_to_wait(void)
{
	LOG_INF("Wait state");

	csr_write(VPRCSR_NORDIC_VPRNORDICSLEEPCTRL,
		  VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_WAIT);
	nrf_barrier_w();
	arch_cpu_idle();
}

#if !defined(CONFIG_SOC_NRF54H20_CPUFLPR)
static void pm_go_to_sleep(void)
{
	LOG_INF("Sleep state");

	csr_write(VPRCSR_NORDIC_VPRNORDICSLEEPCTRL,
		  VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_SLEEP);
	nrf_barrier_w();
	arch_cpu_idle();
}

static void pm_go_to_deep_sleep(void)
{
	LOG_INF("Deep sleep state");

	csr_write(VPRCSR_NORDIC_VPRNORDICSLEEPCTRL,
		  VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_DEEPSLEEP);
	nrf_barrier_w();
	arch_cpu_idle();
}
#endif

static void pm_go_to_hibernate(void)
{
	LOG_INF("Hibernate state (core resets on wakeup)");

	csr_write(VPRCSR_NORDIC_VPRNORDICSLEEPCTRL,
		  VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_HIBERNATE);
	nrf_barrier_w();
	arch_cpu_idle();
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	LOG_INF("pm_state_set: %d %d", (int)state, (int)substate_id);

	switch (state) {
	case PM_STATE_STANDBY:
		switch (substate_id) {
		case VIPER_SLEEP_SUBSTATE_WAIT:
			pm_go_to_wait();
			break;
		}
		break;
	case PM_STATE_SUSPEND_TO_RAM:
		switch (substate_id) {
#if defined(CONFIG_SOC_NRF54H20_CPUFLPR)
		case VIPER_SLEEP_SUBSTATE_HIBERNATE:
			pm_go_to_hibernate();
			break;
		case VIPER_SLEEP_SUBSTATE_DEEPSLEEP:
			/* noop */
			break;
#else
		case VIPER_SLEEP_SUBSTATE_SLEEP:
			pm_go_to_sleep();
			break;
		case VIPER_SLEEP_SUBSTATE_DEEP_SLEEP:
			pm_go_to_deep_sleep();
			break;
		case VIPER_SLEEP_SUBSTATE_HIBERNATE:
			pm_go_to_hibernate();
			break;
#endif
		}
		break;
	default:
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	LOG_INF("pm_state_exit_post_ops");
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	csr_write(VPRCSR_NORDIC_VPRNORDICSLEEPCTRL,
		  VPRCSR_NORDIC_VPRNORDICSLEEPCTRL_SLEEPSTATE_WAIT);

	irq_unlock(MSTATUS_IEN);
}