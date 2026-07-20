/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "app/matter_init.h"
#include "app/task_executor.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/identify-server/identify-server.h>
#include <platform/CHIPDeviceLayer.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;

namespace
{
constexpr EndpointId kEndpointId = 1;
constexpr uint32_t kRelayPulseMs = 2000;

const struct gpio_dt_spec sRelayGpio = GPIO_DT_SPEC_GET(DT_NODELABEL(relay_gpio), gpios);
k_work_delayable sRelayOffWork;

void RelayOffHandler(struct k_work *)
{
	gpio_pin_set_dt(&sRelayGpio, 0);
	LOG_INF("Relay off");
	/* Reflect the momentary auto-off back into the OnOff attribute. */
	SystemLayer().ScheduleLambda([] { Clusters::OnOff::Attributes::OnOff::Set(kEndpointId, false); });
}
} /* namespace */

Identify sIdentify = { kEndpointId, AppTask::IdentifyStartHandler, AppTask::IdentifyStopHandler,
		       Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator };

void AppTask::IdentifyStartHandler(Identify *)
{
	/* No indicator hardware on this board. */
}

void AppTask::IdentifyStopHandler(Identify *)
{
	/* No indicator hardware on this board. */
}

void AppTask::SetRelayState(bool on)
{
	if (on) {
		gpio_pin_set_dt(&sRelayGpio, 1);
		LOG_INF("Relay on");
		/* Momentary buzzer: energize for a fixed window, then release and return to Off. */
		k_work_schedule(&sRelayOffWork, K_MSEC(kRelayPulseMs));
	} else {
		k_work_cancel_delayable(&sRelayOffWork);
		gpio_pin_set_dt(&sRelayGpio, 0);
		LOG_INF("Relay off");
	}
}

CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());

	/* Initialize relay GPIO */
	if (!gpio_is_ready_dt(&sRelayGpio)) {
		LOG_ERR("Relay GPIO not ready");
		return CHIP_ERROR_INTERNAL;
	}
	if (gpio_pin_configure_dt(&sRelayGpio, GPIO_OUTPUT_INACTIVE) < 0) {
		LOG_ERR("Failed to configure relay GPIO");
		return CHIP_ERROR_INTERNAL;
	}
	k_work_init_delayable(&sRelayOffWork, RelayOffHandler);

	return Nrf::Matter::StartServer();
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
