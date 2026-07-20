# Matter Door Lock on Seeed XIAO nRF52840

## Project goal

Port and build Nordic Semiconductor's **Matter door lock sample** for the **Seeed Studio XIAO nRF52840** using **nRF Connect SDK (NCS) v3.1.1**.

The intended design is:

- Matter over Thread
- BLE commissioning
- Persistent Matter settings in internal flash
- Direct boot into the application
- Keep the XIAO's existing UF2 bootloader
- Do **not** add MCUboot
- Do **not** enable Matter OTA/DFU for now

The immediate goal is to obtain a clean firmware build and flashable UF2 image before integrating a physical lock actuator.

---

## Current status

The project configures and builds most of the application, including:

- Zephyr configuration
- Devicetree generation
- Matter ZAP/data-model generation
- Most Zephyr sources
- Most of the Matter/CHIP GN library

The current build stops while compiling:

```text
modules/lib/matter/src/platform/nrfconnect/OTAImageProcessorImpl.cpp
```

with:

```text
error: 'CONFIG_UPDATEABLE_IMAGE_NUMBER' was not declared in this scope
```

The key finding from the **resolved configuration**, rather than only `prj.conf`, is:

```text
CONFIG_CHIP_OTA_REQUESTOR=y
CONFIG_CHIP_BOOTLOADER_NONE=y
```

The generated Matter GN arguments also contain:

```text
chip_enable_ota_requestor = true
```

Therefore, the present problem is no longer MCUboot partition placement. The problem is that the Matter OTA Requestor is still enabled in the final resolved build even though `prj.conf` requests:

```conf
CONFIG_CHIP_OTA_REQUESTOR=n
```

This leaves the build in an inconsistent state:

- Matter bootloader selection: **none**
- MCUboot and image manager: intended to be disabled
- Matter OTA Requestor: still resolves to **enabled**
- OTA implementation expects `CONFIG_UPDATEABLE_IMAGE_NUMBER`
- That symbol is unavailable because there is no MCUboot/updateable-image setup

---

## Development environment

| Item | Value |
|---|---|
| Host OS | Windows |
| Project directory | `C:\myfw\lock` |
| NCS version | `v3.1.1` |
| Zephyr version shown by build | `4.1.99` |
| Board target | `xiao_ble/nrf52840` |
| Toolchain | NCS toolchain `c1a76fddb2` |
| Build system | West + CMake + Ninja + Sysbuild |
| Matter build | Connected Home over IP GN/Ninja build |
| Application | Nordic Matter lock sample |

Build command used by the VS Code extension:

```powershell
west build --build-dir c:/myfw/lock/build c:/myfw/lock `
  --pristine `
  --board xiao_ble/nrf52840 `
  -- -DDEBUG_THREAD_INFO=On `
     -DCONFIG_DEBUG_THREAD_INFO=y `
     -Dlock_DEBUG_THREAD_INFO=On
```

---

## Project structure

```text
C:\myfw\lock
├── boards
│   ├── xiao_ble_nrf52840.conf
│   └── xiao_ble_nrf52840.overlay
├── snippets
├── src
│   ├── access
│   ├── default_zap
│   ├── app_task.cpp
│   ├── bolt_lock_manager.cpp
│   ├── chip_project_config.h
│   ├── main.cpp
│   └── zcl_callbacks.cpp
├── CMakeLists.txt
├── Kconfig
├── prj.conf
├── sysbuild.conf
├── VERSION
├── pm_static_xiao_ble_nrf52840.yml
└── build
```

---

## Hardware

### Confirmed hardware for this project

| Hardware | Purpose/status |
|---|---|
| Seeed Studio XIAO nRF52840 | Target Matter end device |
| nRF52840 SoC | BLE commissioning and 802.15.4/Thread radio |
| XIAO factory/original UF2 bootloader | Must be retained; reason for avoiding MCUboot |
| USB data cable | Power, flashing and serial/USB access |
| Windows development PC | NCS build and flashing environment |
| J-Link/debug interface or available J-Link tooling | Useful for SWD recovery/debugging; a `JLINK` drive/tooling is present in the environment |

### Other development hardware available from the broader Nordic kit inventory

These boards are available, although they are not currently the build target:

- 2 × nRF52840 DK
- 2 × nRF54L15 DK
- 2 × nRF9151 DK
- 2 × Thingy:91
- 1 × Thingy:53
- 1 × nRF7002 DK
- 2 × nPM1300 EK
- 2 × nPM2100 EK
- nRF54H20 DK
- nRF54LM20 DK
- nRF5340 DK
- nRF7002 EBII
- Power Profiler Kit II
- Raspberry Pi 4B
- nRF52840 USB dongle used as an OpenThread RCP
- Google Nest Mini, second generation, usable as a Thread border router

### Lock-specific hardware not yet integrated or confirmed

The firmware port is still at the build stage. No specific physical components have yet been confirmed in this project for:

- Lock motor, solenoid or servo
- Motor driver or MOSFET stage
- External lock power supply
- Door-position sensor
- Bolt-position sensor
- User button
- Status LED beyond the XIAO's onboard hardware
- Enclosure or mechanical lock assembly

These should be selected only after the base Matter application builds and runs reliably.

---

## Configuration files

### `boards/xiao_ble_nrf52840.conf`

Current board-specific options:

```conf
CONFIG_BUILD_OUTPUT_UF2=y
CONFIG_FLASH=y
CONFIG_GPIO=y
```

This file does not enable OTA or MCUboot.

### `sysbuild.conf`

```conf
SB_CONFIG_MATTER=y
SB_CONFIG_BOOTLOADER_MCUBOOT=n
SB_CONFIG_MATTER_FACTORY_DATA_GENERATE=n
```

Intent:

- Enable Matter sysbuild integration
- Do not generate/build MCUboot
- Do not generate Matter factory-data image

### Relevant section of `prj.conf`

```conf
CONFIG_CHIP=y
CONFIG_CHIP_PROJECT_CONFIG="src/chip_project_config.h"
CONFIG_NCS_SAMPLE_MATTER_ZAP_FILE_PATH="${APPLICATION_CONFIG_DIR}/src/default_zap/lock.zap"

CONFIG_CHIP_DEVICE_PRODUCT_ID=32774
CONFIG_STD_CPP17=y

CONFIG_CHIP_BLE_EXT_ADVERTISING=y
CONFIG_CHIP_BLE_ADVERTISING_DURATION=60
CONFIG_BT_DEVICE_NAME="MatterLock"

CONFIG_GPIO=y
CONFIG_DK_LIBRARY=n
CONFIG_PM_DEVICE=y
CONFIG_THREAD_NAME=y

CONFIG_CHIP_FACTORY_DATA=n
CONFIG_CHIP_FACTORY_DATA_BUILD=n
CONFIG_CHIP_NFC_ONBOARDING_PAYLOAD=n

CONFIG_CHIP_OTA_REQUESTOR=n
CONFIG_MCUBOOT_IMG_MANAGER=n
CONFIG_IMG_MANAGER=n
CONFIG_STREAM_FLASH=n
CONFIG_DFU_TARGET=n
CONFIG_DFU_TARGET_MCUBOOT=n
CONFIG_MCUMGR=n
CONFIG_MCUMGR_GRP_IMG=n

CONFIG_CHIP_BOOTLOADER_NONE=y
```

The intent is correct, but `CONFIG_CHIP_OTA_REQUESTOR=n` is not surviving into the final `.config`.

### Static partition file

`pm_static_xiao_ble_nrf52840.yml` currently contains:

```yaml
settings_storage:
  address: 0xf8000
  size: 0x8000
  device: flash_ctrl
  region: flash_primary
```

This reserves the final 32 KiB of the nRF52840's 1 MiB internal flash for Matter/Zephyr settings.

Memory range:

```text
Application/storage boundary: 0xF8000
Settings partition:          0xF8000 - 0xFFFFF
Size:                        0x8000 = 32 KiB
```

The static partition file was initially introduced to provide persistent settings storage while preserving the XIAO bootloader arrangement.

Whether it remains necessary depends on the final storage layout:

- It is **not required merely because Matter is used**.
- It can still be useful to guarantee a known NVS/settings region.
- It must not overlap the XIAO bootloader or any other reserved flash region.
- The exact XIAO factory bootloader flash range still needs to be verified before flashing.

---

## Problems encountered and progress

### 1. Initial build expected an MCUboot primary partition

Initial failure:

```text
error: 'PM_mcuboot_primary_ID' undeclared
```

The failing source was:

```text
zephyr/subsys/dfu/img_util/flash_img.c
```

Interpretation:

- `flash_img.c` had been pulled into the build.
- The flash image subsystem expected an MCUboot upload partition.
- The static partition map only defined `settings_storage`.
- There was no `mcuboot_primary` partition.

This showed that image-management/MCUboot-related features were still active.

### 2. Disabling `IMG_MANAGER` directly did not work

The next pristine configure produced a Kconfig warning/error:

```text
IMG_MANAGER was assigned the value 'n' but got the value 'y'
```

Kconfig explained that it was selected by:

```text
CHIP_BOOTLOADER_MCUBOOT
```

The Matter bootloader choice was resolving to MCUboot despite the attempted negative assignment:

```conf
CONFIG_CHIP_BOOTLOADER_MCUBOOT=n
```

Lesson:

A Kconfig `choice` normally must be resolved by explicitly selecting the desired alternative, rather than only setting one choice entry to `n`.

### 3. Explicitly selected no Matter bootloader

The config was changed to:

```conf
CONFIG_CHIP_BOOTLOADER_NONE=y
```

and the direct line:

```conf
CONFIG_CHIP_BOOTLOADER_MCUBOOT=n
```

was removed.

Additional DFU-related features were explicitly disabled:

```conf
CONFIG_STREAM_FLASH=n
CONFIG_DFU_TARGET=n
CONFIG_DFU_TARGET_MCUBOOT=n
CONFIG_MCUMGR=n
CONFIG_MCUMGR_GRP_IMG=n
```

This got the build past the previous `flash_img.c`/`mcuboot_primary` failure.

### 4. Build advanced into the Matter GN library

After the changes, the build successfully reached:

```text
[379/482] Performing build step for 'chip-gn'
```

Matter data-model generation completed, including generated headers for:

- Door Lock
- Identify
- Administrator Commissioning
- Operational Credentials
- Group Key Management
- ICD Management
- Network diagnostics

The GN generator created thousands of Matter targets and compiled a substantial portion of the library.

### 5. Current failure: OTA implementation still included

The build now fails in:

```text
OTAImageProcessorImpl.cpp
```

at:

```cpp
for (int image_id = 0;
     image_id < CONFIG_UPDATEABLE_IMAGE_NUMBER;
     ++image_id)
```

because:

```text
CONFIG_UPDATEABLE_IMAGE_NUMBER
```

is not defined.

This is consistent with trying to compile the OTA image processor while no updateable image/MCUboot configuration exists.

### 6. Resolved configuration exposed the real mismatch

The uploaded generated `.config` contains:

```text
CONFIG_CHIP_OTA_REQUESTOR=y
CONFIG_CHIP_BOOTLOADER_NONE=y
```

It also contains expected persistent-storage options:

```text
CONFIG_NVS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_PARTITION_MANAGER_ENABLED=y
CONFIG_FLASH_MAP=y
```

The uploaded generated `args.gn` contains:

```text
chip_enable_ota_requestor = true
```

This proves that the current build problem is not a stale guess based only on `prj.conf`:

> The final Kconfig/GN configuration is actively enabling the Matter OTA Requestor.

---

## Current blocker

We need to determine **why `CONFIG_CHIP_OTA_REQUESTOR` resolves to `y`** even though `prj.conf` sets it to `n`.

Possible sources include:

1. A default in Nordic's Matter Kconfig files.
2. A sample-common Kconfig selection.
3. A dependency or feature that selects the OTA Requestor.
4. The lock sample's inherited `Kconfig.defaults`.
5. A board/sample fragment merged after `prj.conf`.
6. An NCS v3.1.1 limitation or bug in the no-bootloader Matter configuration.

The local application `Kconfig` sources:

```text
${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/chip-module/Kconfig.features
${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/chip-module/Kconfig.defaults
${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/src/Kconfig
Kconfig.zephyr
```

The exact symbol definition and reverse dependency must be inspected in those NCS files.

---

## Recommended next investigation

### 1. Inspect the symbol in menuconfig

Run:

```powershell
west build -t menuconfig -d build/lock
```

Search for:

```text
CHIP_OTA_REQUESTOR
```

Inspect:

- Prompt and current value
- Direct dependencies
- Defaults
- `Selected by` / reverse dependencies
- Location of the symbol definition

This is the cleanest way to see why Kconfig forces the symbol to `y`.

### 2. Search the NCS source tree

From an NCS terminal:

```powershell
rg -n "config CHIP_OTA_REQUESTOR|select CHIP_OTA_REQUESTOR|default y.*CHIP_OTA_REQUESTOR" `
  C:\ncs\v3.1.1\modules\lib\matter `
  C:\ncs\v3.1.1\nrf
```

Also inspect all references:

```powershell
rg -n "CHIP_OTA_REQUESTOR" `
  C:\ncs\v3.1.1\modules\lib\matter `
  C:\ncs\v3.1.1\nrf
```

### 3. Determine whether another symbol selects it

If a symbol uses:

```kconfig
select CHIP_OTA_REQUESTOR
```

then assigning:

```conf
CONFIG_CHIP_OTA_REQUESTOR=n
```

cannot override that selection. The selecting feature must be disabled instead.

### 4. Verify the generated state after every change

After a pristine configure:

```powershell
rg "CONFIG_CHIP_OTA_REQUESTOR|CONFIG_CHIP_BOOTLOADER|CONFIG_IMG_MANAGER|CONFIG_UPDATEABLE_IMAGE_NUMBER" `
  build\lock\zephyr\.config

rg -i "ota|requestor|bootloader|updateable" `
  build\lock\modules\connectedhomeip\args.gn
```

Expected no-OTA state:

```text
# CONFIG_CHIP_OTA_REQUESTOR is not set
CONFIG_CHIP_BOOTLOADER_NONE=y
```

and GN should show:

```text
chip_enable_ota_requestor = false
```

or omit the enabled OTA argument.

### 5. Do not mask the failure with a fake image count

A temporary line such as:

```conf
CONFIG_UPDATEABLE_IMAGE_NUMBER=1
```

might get past this specific compiler error, but it would leave OTA code enabled without a valid update mechanism or image partition arrangement. That is not the desired architecture and may cause later link or runtime failures.

The correct fix is to stop building the OTA Requestor and `OTAImageProcessorImpl.cpp`.

---

## Partition-file decision

For now, keep `pm_static_xiao_ble_nrf52840.yml` available but treat it separately from the OTA problem.

The current compiler failure is caused by OTA configuration, not by the settings partition.

After OTA is disabled:

1. Try a pristine build with the static settings partition.
2. Verify generated `partitions.yml` and `pm.config`.
3. Confirm no overlap with the XIAO UF2 bootloader.
4. Flash only after confirming the application image start/end addresses.
5. If Partition Manager can automatically create a correct settings region, test whether the static file can be removed.

Useful generated files:

```text
build\lock\partitions.yml
build\lock\pm.config
build\lock\zephyr\zephyr.map
```

---

## Success criteria for the next milestone

The firmware build milestone is complete when:

- `CONFIG_CHIP_BOOTLOADER_NONE=y`
- `CONFIG_CHIP_OTA_REQUESTOR` is not set
- `chip_enable_ota_requestor` is false
- No `OTAImageProcessorImpl.cpp` is compiled
- No `flash_img.c` MCUboot partition error occurs
- Matter lock application links successfully
- A UF2 output is generated
- Flash layout is confirmed not to overwrite the XIAO bootloader
- Firmware boots and exposes logs
- BLE Matter commissioning begins
- Device joins a Thread network
- Door Lock endpoint responds to Matter commands

---

## Flashing caution

Do not flash a full-chip image until the XIAO bootloader location and generated application range are verified.

Before flashing:

```powershell
west build -d build -t partition_manager_report
```

or inspect:

```text
build\lock\partitions.yml
build\lock\zephyr\zephyr.map
```

Preserving the existing XIAO UF2 bootloader is a core project requirement.

---

## Summary

We successfully:

- Adapted the Nordic Matter lock sample toward the XIAO board.
- Enabled XIAO-specific UF2 output.
- Removed Nordic DK-library dependence.
- Disabled sysbuild MCUboot generation.
- Selected the Matter `NONE` bootloader choice.
- Disabled multiple Zephyr DFU/image-management features.
- Got past the missing `mcuboot_primary` partition failure.
- Reached the Matter GN library build.
- Identified the present failure as an OTA Requestor configuration mismatch.
- Confirmed from generated files that OTA is still actually enabled:
  - `CONFIG_CHIP_OTA_REQUESTOR=y`
  - `chip_enable_ota_requestor = true`

The project is currently **not yet building**, but the remaining issue is narrowed down clearly:

> Find and disable the Kconfig feature that forces the Matter OTA Requestor on in NCS v3.1.1 while using `CONFIG_CHIP_BOOTLOADER_NONE=y`.
