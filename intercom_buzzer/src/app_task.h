/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <platform/CHIPDeviceLayer.h>

struct Identify;

class AppTask {
public:
	static AppTask &Instance()
	{
		static AppTask sAppTask;
		return sAppTask;
	};

	CHIP_ERROR StartApp();

	static void IdentifyStartHandler(Identify *);
	static void IdentifyStopHandler(Identify *);

	/* Drive the relay from the Matter On/Off attribute-change callback. */
	static void SetRelayState(bool on);

private:
	CHIP_ERROR Init();
};
