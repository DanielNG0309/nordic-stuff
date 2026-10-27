/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"
#include "bolt_lock_manager.h"

#include "app/matter_init.h"
#include "app/task_executor.h"

#ifdef CONFIG_CHIP_NUS
#include "bt_nus/bt_nus_service.h"
#endif

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app/clusters/identify-server/identify-server.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

static const struct gpio_dt_spec sRelayGpio = GPIO_DT_SPEC_GET(DT_NODELABEL(relay_gpio), gpios);
static struct k_work_delayable sRelayOffWork;

static void RelayOffHandler(struct k_work *)
{
	gpio_pin_set_dt(&sRelayGpio, 0);
	LOG_INF("Relay off");
	BoltLockMgr().Lock(BoltLockManager::OperationSource::kUnspecified);
}

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;

namespace
{
constexpr EndpointId kLockEndpointId = 1;

#ifdef CONFIG_CHIP_NUS
constexpr uint16_t kAdvertisingIntervalMin = 400;
constexpr uint16_t kAdvertisingIntervalMax = 500;
constexpr uint8_t kLockNUSPriority = 2;
#endif

#ifndef CONFIG_CHIP_FACTORY_RESET_ERASE_SETTINGS
void AppFactoryResetHandler(const ChipDeviceEvent *event, intptr_t /* unused */)
{
	switch (event->Type) {
	case DeviceEventType::kFactoryReset:
		BoltLockMgr().FactoryReset();
		break;
	default:
		break;
	}
}
#endif

} /* namespace */

Identify sIdentify = { kLockEndpointId, AppTask::IdentifyStartHandler, AppTask::IdentifyStopHandler,
		       Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator };

void AppTask::IdentifyStartHandler(Identify *)
{
	/* No LED available */
}

void AppTask::IdentifyStopHandler(Identify *)
{
	/* No LED available */
}

void AppTask::LockStateChanged(const BoltLockManager::StateData &stateData)
{
	switch (stateData.mState) {
	case BoltLockManager::State::kLockingInitiated:
		LOG_INF("Lock action initiated");
#ifdef CONFIG_CHIP_NUS
		Nrf::GetNUSService().SendData("locking", sizeof("locking"));
#endif
		break;
	case BoltLockManager::State::kLockingCompleted:
		LOG_INF("Lock action completed");
#ifdef CONFIG_CHIP_NUS
		Nrf::GetNUSService().SendData("locked", sizeof("locked"));
#endif
		break;
	case BoltLockManager::State::kUnlockingInitiated:
		LOG_INF("Unlock action initiated");
#ifdef CONFIG_CHIP_NUS
		Nrf::GetNUSService().SendData("unlocking", sizeof("unlocking"));
#endif
		break;
	case BoltLockManager::State::kUnlockingCompleted:
		LOG_INF("Unlock action completed");
		gpio_pin_set_dt(&sRelayGpio, 1);
		LOG_INF("Relay on");
		k_work_schedule(&sRelayOffWork, K_SECONDS(2));
#ifdef CONFIG_CHIP_NUS
		Nrf::GetNUSService().SendData("unlocked", sizeof("unlocked"));
#endif
		break;
	}

	/* Handle changing attribute state in the application */
	Instance().UpdateClusterState(stateData);
}

void AppTask::UpdateClusterState(const BoltLockManager::StateData &stateData)
{
	BoltLockManager::StateData *stateDataCopy = Platform::New<BoltLockManager::StateData>(stateData);

	if (stateDataCopy == nullptr) {
		LOG_ERR("Failed to allocate memory for BoltLockManager::StateData");
		return;
	}

	CHIP_ERROR err = SystemLayer().ScheduleLambda([stateDataCopy]() {
		UpdateClusterStateHandler(*stateDataCopy);
		Platform::Delete(stateDataCopy);
	});

	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Failed to schedule lambda: %" CHIP_ERROR_FORMAT, err.Format());
		Platform::Delete(stateDataCopy);
	}
}

void AppTask::UpdateClusterStateHandler(const BoltLockManager::StateData &stateData)
{
	using namespace chip::app::Clusters::DoorLock::Attributes;

	DlLockState newLockState;

	switch (stateData.mState) {
	case BoltLockManager::State::kLockingCompleted:
		newLockState = DlLockState::kLocked;
		break;
	case BoltLockManager::State::kUnlockingCompleted:
		newLockState = DlLockState::kUnlocked;
		break;
	default:
		newLockState = DlLockState::kNotFullyLocked;
		break;
	}

	Nullable<DlLockState> currentLockState;
	LockState::Get(kLockEndpointId, currentLockState);

	if (currentLockState.IsNull()) {
		LockState::Set(kLockEndpointId, newLockState);
	} else {
		LOG_INF("Updating LockState attribute");

		Nullable<uint16_t> userId;
		Nullable<List<const LockOpCredentials>> credentials;
#ifdef CONFIG_LOCK_PASS_CREDENTIALS_TO_SET_LOCK_STATE
		List<const LockOpCredentials> credentialList;
#endif

		if (!stateData.mValidatePINResult.IsNull()) {
			userId = { stateData.mValidatePINResult.Value().mUserId };

#ifdef CONFIG_LOCK_PASS_CREDENTIALS_TO_SET_LOCK_STATE
			credentialList = { &stateData.mValidatePINResult.Value().mCredential, 1 };
			credentials = { credentialList };
#endif
		}

		if (!DoorLockServer::Instance().SetLockState(kLockEndpointId, newLockState, stateData.mSource, userId,
							     credentials, stateData.mFabricIdx, stateData.mNodeId)) {
			LOG_ERR("Failed to update LockState attribute");
		}
	}
}

#ifdef CONFIG_CHIP_NUS
void AppTask::NUSLockCallback(void *context)
{
	LOG_DBG("Received LOCK command from NUS");
	if (BoltLockMgr().GetState().mState == BoltLockManager::State::kLockingCompleted ||
	    BoltLockMgr().GetState().mState == BoltLockManager::State::kLockingInitiated) {
		LOG_INF("Device is already locked");
	}
}

void AppTask::NUSUnlockCallback(void *context)
{
	LOG_DBG("Received UNLOCK command from NUS");
	if (BoltLockMgr().GetState().mState == BoltLockManager::State::kUnlockingCompleted ||
	    BoltLockMgr().GetState().mState == BoltLockManager::State::kUnlockingInitiated) {
		LOG_INF("Device is already unlocked");
	}
}
#endif

#ifdef CONFIG_NCS_SAMPLE_MATTER_TEST_EVENT_TRIGGERS
CHIP_ERROR AppTask::DoorLockJammedEventCallback(Nrf::Matter::TestEventTrigger::TriggerValue)
{
	VerifyOrReturnError(DoorLockServer::Instance().SendLockAlarmEvent(kLockEndpointId, AlarmCodeEnum::kLockJammed),
			    CHIP_ERROR_INTERNAL);
	LOG_ERR("Event Trigger: Doorlock jammed.");
	return CHIP_NO_ERROR;
}
#endif

CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());

#ifndef CONFIG_CHIP_FACTORY_RESET_ERASE_SETTINGS
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(AppFactoryResetHandler, 0));
#endif

#ifdef CONFIG_CHIP_NUS
	if (!Nrf::GetNUSService().Init(kLockNUSPriority, kAdvertisingIntervalMin, kAdvertisingIntervalMax)) {
		ChipLogError(Zcl, "Cannot initialize NUS service");
	}
	Nrf::GetNUSService().RegisterCommand("Lock", sizeof("Lock"), NUSLockCallback, nullptr);
	Nrf::GetNUSService().RegisterCommand("Unlock", sizeof("Unlock"), NUSUnlockCallback, nullptr);
	if (!Nrf::GetNUSService().StartServer()) {
		LOG_ERR("GetNUSService().StartServer() failed");
	}
#endif

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

	/* Initialize lock manager */
	BoltLockMgr().Init(LockStateChanged);

#ifdef CONFIG_NCS_SAMPLE_MATTER_TEST_EVENT_TRIGGERS
	ReturnErrorOnFailure(Nrf::Matter::TestEventTrigger::Instance().RegisterTestEventTrigger(
		kDoorLockJammedEventTriggerId,
		Nrf::Matter::TestEventTrigger::EventTrigger{ 0, DoorLockJammedEventCallback }));
#endif

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
