/*
 * Battery level: the SAADC reads VBAT through the XIAO's 1M/510k divider, a LiPo discharge
 * curve turns the voltage into a percentage, and the result goes to the Matter Power Source
 * cluster on the lock endpoint.
 */

#include "battery.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/power-source-server/power-source-server.h>
#include <platform/CHIPDeviceLayer.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app::Clusters;
using namespace ::chip::DeviceLayer;

namespace
{
constexpr EndpointId kPowerSourceEndpointId = 1;

/* Divider: VBAT -> 1M -> AIN7 -> 510k -> GND (P0.14) */
constexpr int32_t kDividerFullOhms = 1510;
constexpr int32_t kDividerOutputOhms = 510;

constexpr uint32_t kUpdateIntervalS = 600;
/* After the relay coil (~120 mA for 2 s) lets go, the cell takes a moment to recover */
constexpr uint32_t kLoadSettleS = 10;

/* BatChargeLevel thresholds, % */
constexpr uint8_t kWarningPercent = 20;
constexpr uint8_t kCriticalPercent = 10;

/* Every attribute change is a report to Home, and a report wakes the SED's radio: send only
 * changes this large.
 */
constexpr uint32_t kVoltageReportStepMv = 50;
constexpr uint8_t kPercent2xReportStep = 4;

/* Resting LiPo voltage, mV -> % remaining; a generic single-cell curve, not a profile of this
 * cell.
 */
struct CurvePoint {
	int32_t mv;
	int32_t percent;
};
constexpr CurvePoint kCurve[] = {
	{ 4200, 100 }, { 4100, 90 }, { 4000, 80 }, { 3920, 70 }, { 3870, 60 }, { 3820, 50 },
	{ 3790, 40 },  { 3770, 30 }, { 3740, 20 }, { 3680, 10 }, { 3450, 5 },  { 3300, 0 },
};

const struct adc_dt_spec sAdc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
const struct gpio_dt_spec sEnable = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), vbat_enable_gpios);
const struct gpio_dt_spec sCharging = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), charging_gpios);

struct k_work_delayable sUpdateWork;

int32_t PercentFromMv(int32_t mv)
{
	if (mv >= kCurve[0].mv) {
		return 100;
	}
	for (size_t i = 1; i < ARRAY_SIZE(kCurve); i++) {
		const CurvePoint &hi = kCurve[i - 1];
		const CurvePoint &lo = kCurve[i];

		if (mv >= lo.mv) {
			return lo.percent + (mv - lo.mv) * (hi.percent - lo.percent) / (hi.mv - lo.mv);
		}
	}
	return 0;
}

int ReadMv(int32_t &mv)
{
	int16_t raw;
	struct adc_sequence seq = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int ret = adc_sequence_init_dt(&sAdc, &seq);

	if (ret == 0) {
		ret = adc_read_dt(&sAdc, &seq);
	}
	if (ret < 0) {
		return ret;
	}

	mv = raw;
	ret = adc_raw_to_millivolts_dt(&sAdc, &mv);
	if (ret < 0) {
		return ret;
	}
	mv = mv * kDividerFullOhms / kDividerOutputOhms;
	return 0;
}

/* Runs on the Matter thread. Only changed values are written, see kVoltageReportStepMv. */
void Publish(uint8_t percent2x, uint32_t mv, PowerSource::BatChargeLevelEnum level,
	     PowerSource::BatChargeStateEnum chgState)
{
	using namespace PowerSource::Attributes;

	static bool sPublished;
	static uint8_t sPercent2x;
	static uint32_t sMv;
	static PowerSource::BatChargeLevelEnum sLevel;
	static PowerSource::BatChargeStateEnum sChgState;

	if (!sPublished || (percent2x > sPercent2x ? percent2x - sPercent2x : sPercent2x - percent2x) >=
				   kPercent2xReportStep) {
		BatPercentRemaining::Set(kPowerSourceEndpointId, percent2x);
		sPercent2x = percent2x;
	}
	if (!sPublished || (mv > sMv ? mv - sMv : sMv - mv) >= kVoltageReportStepMv) {
		BatVoltage::Set(kPowerSourceEndpointId, mv);
		sMv = mv;
	}
	if (!sPublished || level != sLevel) {
		BatChargeLevel::Set(kPowerSourceEndpointId, level);
		sLevel = level;
	}
	if (!sPublished || chgState != sChgState) {
		BatChargeState::Set(kPowerSourceEndpointId, chgState);
		sChgState = chgState;
	}
	sPublished = true;
}

void Update(struct k_work *)
{
	int32_t mv;
	int ret = ReadMv(mv);

	k_work_reschedule(&sUpdateWork, K_SECONDS(kUpdateIntervalS));
	if (ret < 0) {
		LOG_ERR("Battery read failed: %d", ret);
		return;
	}

	const bool charging = gpio_pin_get_dt(&sCharging) == 1;
	const int32_t percent = PercentFromMv(mv);
	const uint8_t percent2x = static_cast<uint8_t>(percent * 2);
	const auto level = percent < kCriticalPercent ? PowerSource::BatChargeLevelEnum::kCritical :
			   percent < kWarningPercent  ? PowerSource::BatChargeLevelEnum::kWarning :
							PowerSource::BatChargeLevelEnum::kOk;
	const auto chgState =
		charging ? PowerSource::BatChargeStateEnum::kIsCharging : PowerSource::BatChargeStateEnum::kIsNotCharging;

	LOG_INF("Battery %d mV, %d %%%s", mv, percent, charging ? ", charging" : "");

	CHIP_ERROR err = SystemLayer().ScheduleLambda([percent2x, mv, level, chgState] {
		Publish(percent2x, static_cast<uint32_t>(mv), level, chgState);
	});

	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Failed to schedule the battery report: %" CHIP_ERROR_FORMAT, err.Format());
	}
}
} /* namespace */

namespace Battery
{

int Init()
{
	if (!adc_is_ready_dt(&sAdc) || !gpio_is_ready_dt(&sEnable) || !gpio_is_ready_dt(&sCharging)) {
		LOG_ERR("Battery ADC or GPIO not ready");
		return -ENODEV;
	}

	/* Active = P0.14 low, see the board overlay */
	int ret = gpio_pin_configure_dt(&sEnable, GPIO_OUTPUT_ACTIVE);

	if (ret == 0) {
		ret = gpio_pin_configure_dt(&sCharging, GPIO_INPUT);
	}
	if (ret == 0) {
		ret = adc_channel_setup_dt(&sAdc);
	}
	if (ret < 0) {
		LOG_ERR("Battery setup failed: %d", ret);
		return ret;
	}

	/* The battery powers the whole node */
	SystemLayer().ScheduleLambda([] {
		EndpointId endpoints[] = { 0, kPowerSourceEndpointId };
		CHIP_ERROR err = PowerSourceServer::Instance().SetEndpointList(kPowerSourceEndpointId,
										  Span<EndpointId>(endpoints));

		if (err != CHIP_NO_ERROR) {
			LOG_ERR("Power Source endpoint list not set: %" CHIP_ERROR_FORMAT, err.Format());
		}
	});

	k_work_init_delayable(&sUpdateWork, Update);
	k_work_schedule(&sUpdateWork, K_NO_WAIT);
	return 0;
}

void NoteLoad()
{
	/* Once the relay's 2 s pulse is over and the cell has recovered */
	k_work_reschedule(&sUpdateWork, K_SECONDS(2 + kLoadSettleS));
}

} /* namespace Battery */
