# Intercom buzzer

Matter-over-Thread door lock on a Seeed XIAO nRF52840 that pulses a relay for 2 s to short an
apartment intercom's door-release button, so the street door can be opened from Google Home.
It shows up as a door lock: "unlock" buzzes the door, then it goes back to locked.

Based on the NCS v3.1.0 `samples/matter/lock` sample, without MCUboot: the XIAO keeps its
UF2 bootloader and boots straight into the app.

## Hardware

| Part | Notes |
|---|---|
| Seeed XIAO nRF52840 | Battery soldered to the BAT+/BAT- pads, so the on-board BQ25101 charges it from USB. The pads are fragile: avoid reworking them once a lead is soldered. |
| Relay module, Songle SRD-03VDC-SL-C with EL817 optocoupler | 3-3.3 V logic, ~3 mA trigger current, flyback diode on the module, no power LED. Jumpers in the default (shared power) position. |
| LiPo, 3.7 V 620 mAh | About 6-7 h to charge from empty at 100 mA. |
| Thread border router | Google Nest Hub (2nd gen). |

## Wiring

```
XIAO 3V3 → Relay VCC
XIAO GND → Relay GND
XIAO D2  → Relay IN
Relay NO + COM → intercom door-release button terminals (in parallel)
```

## Firmware

- Board target `xiao_ble/nrf52840`, NCS v3.1.0.
- Door Lock device (VID 0xFFF1 / PID 0x8006, the SDK's stock test DAC for that PID), serial
  `BUZZER0001`. A door lock rather than an on/off plug, so "turn everything off" can't buzz
  the door.
- Thread SED, 1 s slow poll.
- Factory data at 0xe5000 (4 KB, `pm_static_xiao_ble_nrf52840.yml`), settings
  0xe6000-0xf3fff, Adafruit UF2 bootloader from 0xf4000.
- Shells (Zephyr and Matter) on the USB CDC console; they only work with USB connected.
- `uart0` is disabled in `boards/xiao_ble_nrf52840.overlay`. The board enables it, but the
  console is on USB CDC. With the shell's interrupt-driven UART mode, the driver starts its
  receiver at boot and nothing stops it, which keeps the HF clock running (0.5-1 mA).

Onboarding (test values):

```
QRCode:            MT:8IXS142C00KA0648G00
ManualPairingCode: 34970112332
Discriminator:     3840 (0xF00)
Passcode:          20202021
```

### Battery level

The lock endpoint has a Power Source cluster (battery, rechargeable), so Google Home shows the
battery. `src/battery.cpp` reads VBAT on P0.31 (AIN7) through the XIAO's 1M/510k divider every
10 min and 12 s after each buzz (once the relay's sag is over). It maps the voltage to a
percentage with a generic LiPo curve, so treat it as a gauge rather than a fuel meter, and
reports `BatPercentRemaining`, `BatVoltage`, `BatChargeLevel` (warning below 20 %, critical
below 10 %) and `BatChargeState` (from the charger's CHG output on P0.17). Only changes of 2 %
or 50 mV are reported, since every report wakes the radio. While charging, the voltage and the
percentage read high.

P0.14, the bottom of the divider, is held low all the time (~2.7 µA at 4.2 V): high or
floating, it would put VBAT on P0.31 through the 1M.

After a change to `src/default_zap/lock.zap`, regenerate `zap-generated/` and `lock.matter`
with `west zap-generate`.

### Power

| Item | Notes |
|---|---|
| Thread | SED, 1 s slow poll, ICD active mode 1000 ms. |
| External flash | The XIAO's P25Q16H (QSPI) is put in deep power-down at startup by NCS's `matter_init.cpp` (`CONFIG_PM_DEVICE` + QSPI NOR driver). |
| BLE | No advertising in normal use: no SMP, and the commissioning advertising stops after 15 min. |
| Unused RAM | Powered down only in `prj_release.conf` (`CONFIG_RAM_POWER_DOWN_LIBRARY`). |
| Relay coil | ~120 mA at 3 V for the 2 s pulse, ~67 µAh per buzz: at 5 buzzes a day about 120 mAh a year. |
| Battery divider | ~2.7 µA (above). |

To measure the floor: a PPK2 in source meter mode on the BAT+/BAT- pads at 3.9 V, with the
battery and USB unplugged. Not in the battery's ground lead: the PPK2's ampere meter needs
0.8-5 V on VIN, so in the ground return it starves the load.

## Build and flash

```
west build -p -b xiao_ble/nrf52840
```

The XIAO keeps its UF2 bootloader, so flash the app region only:

1. Trim `build/merged.hex` to 0x27000-0xf3fff (leaves the MBR and the bootloader alone; the
   settings, which hold the fabric, aren't in the image, so they survive).
2. Enter the bootloader with a double press of reset (a triple press on some boards).
3. Either convert to UF2 (`uf2conv.py -f 0xADA52840`) and copy it to the XIAO's drive, or use
   the bootloader's serial DFU:
   ```
   adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0xFFFE --application trimmed.hex buzzer_dfu.zip
   adafruit-nrfutil dfu serial --package buzzer_dfu.zip -p <port> -b 115200 --singlebank
   ```

## Commission

1. Factory-reset on the shell: `matter device factoryreset`. It reboots and, with
   `CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART=y`, opens the commissioning window for 15 minutes
   (`CHIP_BLE_ADVERTISING_DURATION=15`). After that, `matter ble adv start` only restarts BLE
   advertising, not the commissioning window: factory-reset again instead.
2. In Google Home: + → Device → Matter-enabled device, scan the QR code.

Google Home notes for test devices:

- It keeps one device per test VID/PID with the stock DAC; a different serial number is not
  enough. Give each test device its own PID (Nordic ships stock test DACs for 0x8000-0x801F).
- Self-made test DACs are refused ("Failed generating device credentials"), even though they
  chain to the test PAA.
- Once the Google account has a Developer Console project with test-VID integrations, Google
  only issues credentials for registered VID/PIDs: register each one (type Lock).
- A slow "Generating Matter credentials" screen with clean device logs is on Google's side;
  retrying works.
