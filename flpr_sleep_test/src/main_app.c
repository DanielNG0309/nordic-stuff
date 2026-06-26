#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <hal/nrf_memconf.h>

LOG_MODULE_REGISTER(flpr_sleep_test, LOG_LEVEL_INF);


/*
 * The app core uses CONFIG_PM so the CPU enters a low power state during
 * k_sleep(K_FOREVER). These stubs satisfy the linker
 */
void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
	irq_unlock(0);
}

int main(void)
{
#if !defined(CONFIG_TEST_BASELINE_SLEEP)
	LOG_INF("Multicore flpr_sleep_test on %s", CONFIG_BOARD_TARGET);
	LOG_INF("App core sleeping, FLPR running independently");
#endif

#if defined(CONFIG_SOC_NRF54LM20A) || defined(CONFIG_SOC_NRF54LM20A_ENGA)

	
	/* Disable unused blocks 1-14 */
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM1_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM2_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM3_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM4_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM5_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM6_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM7_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM8_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM9_Pos,  false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM10_Pos, false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM11_Pos, false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM12_Pos, false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM13_Pos, false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM14_Pos, false);
	
	/* Keep block 15 - FLPR VPR save area (RAM01 section 7) */
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM15_Pos, true);
	/* Enable VPR context restore feature (block 32 = POWER[1].RET.MEM[0]) */
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 1, MEMCONF_POWER_RET_MEM0_Pos, true);


#elif defined(CONFIG_SOC_NRF54L15)

	/* Disable unused APP SRAM blocks 1-4 */
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM1_Pos, false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM2_Pos, false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM3_Pos, false);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM4_Pos, false);
	/* Keep FLPR SRAM blocks 5-7 (0x20028000, 96 KB) */
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM5_Pos, true);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM6_Pos, true);
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 0, MEMCONF_POWER_RET_MEM7_Pos, true);
	/* Enable VPR context restore feature (block 32 = POWER[1].RET.MEM[0]) */
	nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF, 1, MEMCONF_POWER_RET_MEM0_Pos, true);

#endif /* SOC selection */

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}