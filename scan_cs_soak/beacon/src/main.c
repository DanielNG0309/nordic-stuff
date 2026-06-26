/*
 * scan_cs_soak — BEACON
 *
 * A constant non-connectable advertiser. Its only job is to emit a steady stream
 * of advertising packets so the gateway can measure that its scanner keeps
 * receiving reports across every CS cycle (scanner-health probe).
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>

static const char beacon_str[] = "SOAK BEACON";
static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, "SOAK BEACON", sizeof(beacon_str) - 1),
};

int main(void)
{
	int err;

	printk("\n*** scan_cs_soak BEACON — constant non-connectable advertiser ***\n");

	err = bt_enable(NULL);
	if (err) {
		printk("bt_enable (%d)\n", err);
		return 0;
	}

	/* Non-connectable, fast interval (~100-150 ms) for a dense report stream. */
	err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_NONE, BT_GAP_ADV_FAST_INT_MIN_2,
					      BT_GAP_ADV_FAST_INT_MAX_2, NULL),
			      ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("adv_start (%d)\n", err);
		return 0;
	}

	printk("[beacon] advertising as '%s'\n", beacon_str);
	return 0;
}
