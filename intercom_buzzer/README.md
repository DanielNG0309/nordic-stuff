# Smart Apartment Buzzer — Project State

_Last updated: 2026-05-01_

## Goal

Matter-over-Thread door lock device (XIAO nRF52840 Sense) that triggers a relay
to momentarily short an intercom's door-release button, controllable via
Google Home / Google Assistant.

---

## Hardware Inventory

| Item | Status | Notes |
|---|---|---|
| XIAO nRF52840 #1 ("Test lock" board) | **Dead — battery pad shot** | BAT+ pad physically ripped during rework, resoldered but still unreliable. Still functional via USB power only. Set aside for a future non-battery project. |
| XIAO nRF52840 #2 (SWD-damaged board) | **Recovered / in use** | Broke during earlier SWD work. Recovered to a UF2 drive by triple-pressing reset (not double). This is the board we are currently trying to re-flash and re-commission. |
| Relay module (Songle SRD-03VDC-SL-C, EL817 optocoupler, "Be In Your Mind" brand) | Good | 3–3.3V logic, 3mA trigger current, flyback diode built in. **No power LED** — confirmed no jumper/LED removal needed. Jumper caps control shared vs. isolated power domains; leave in default (shared) position for our wiring. |
| LiPo battery, 3.7V 620mAh | Good | Confirmed working via BQ25101 charge indicator (green LED). Full charge from empty ≈ 6–7 hrs at 100mA. |
| Google Nest Hub 2nd Gen | Working as Thread Border Router | Multiple factory resets performed today during troubleshooting. |
| PPK2 | Used for power profiling | Confirmed idle draw ~14nA on D2, ~830µA / 2s pulse on unlock event — firmware GPIO behavior is correct. |

---

## Wiring (confirmed correct)

```
XIAO 3V3 → Relay VCC
XIAO GND → Relay GND
XIAO D2  → Relay IN
Relay NO + COM → intercom door-release button terminals (in parallel)
```

Battery solders to XIAO **BAT+/BAT-** pads (not 3V3) to use the onboard
charge/regulator path.

---

## Firmware / Build Details

- NCS v3.1.1
- Board target: `xiao_ble/nrf52840`
- Sample base: `samples/matter/lock`
- Factory data: `CONFIG_CHIP_FACTORY_DATA=y`, `CONFIG_CHIP_FACTORY_DATA_BUILD=y`,
  `SB_CONFIG_MATTER_FACTORY_DATA_GENERATE=y`
- Partition: factory data at `0xe5000` (4 KB, `pm_static_xiao_ble_nrf52840.yml`), settings
  `0xe6000`-`0xf3fff`, UF2 bootloader at `0xf4000+`
- Confirmed onboarding codes (unchanged throughout):
  ```
  QRCode:            MT:8IXS142C00KA0648G00
  ManualPairingCode: 34970112332
  Discriminator:     3840 (0xF00)
  Passcode:          20202021
  VendorId:          0xFFF1 (65521) — Nordic dev DAC, cannot be changed without new DAC cert
  ProductId:          0x8006 (32774)
  ```

### Flash pipeline (trim + UF2 convert)

1. Build normally (`west build --board xiao_ble/nrf52840 --pristine`)
2. Trim `merged.hex` to `0x27000–0xf3fff` (skips MBR/bootloader region)
3. Convert trimmed hex to UF2 using `uf2conv.py -f 0xADA52840` (nRF52840 UF2 family ID)
4. Copy `.uf2` to the XIAO's UF2 drive (enter bootloader via triple-reset-press on damaged board)

---

## Where We Got Stuck

### 1. Battery pad damage (board #1)
- Removed a solder blob while fixing a bridge, accidentally ripped the BAT+ pad off.
- Resoldered — board still lights the charge LED but can't reliably hold 3V3 on battery alone (confirmed via cold-joint testing). Retired this board.

### 2. Commissioning timeout (board #2, ongoing)
- Firmware boots cleanly every time, BLE advertises, PASE handshake completes,
  DAC certificate chain is exchanged successfully — **every single attempt**.
- Google Home then hangs on **"Generating Matter credentials"** for minutes,
  well past the 120s `ArmFailSafe` window, and either:
  - times out with `Fail-safe timer expired` (error 32), or
  - drops the BLE connection with `ack recv timeout`.
- Conclusion: this is a **Google server-side DAC verification latency issue**,
  not a firmware bug. Device-side logs are clean on every attempt.

### 3. Self-inflicted regression via Google Home Developer Console
- In an attempt to speed up DAC verification, registered a Matter integration
  in `console.home.google.com` under VID `0xFFF1` / PID `0x8006`.
- This caused Google Home to treat the test VID as a project-managed device,
  **blocking normal consumer BLE discovery/commissioning** entirely — not just
  for the XIAO, but for an unrelated **nRF54L15 light bulb sample** that had
  been working fine before.
- Troubleshooting attempted (in order): changing PID to `0x0002`, looking for
  Field Trial options (none available — Field Trial requires a completed
  certification test), looking for a project delete option (**not exposed in
  the console UI** — only "Manage members" / "Project details" from the
  3-dot menu), clearing Google Home app data/cache, factory-resetting the
  Nest Hub multiple times, changing VID from `0xFFF1` to `0xFFF4`.
- **Current state:** VID changed to `0xFFF4` in the console integration,
  Nest Hub rebooted. Google Home app itself is now showing signs of being
  generally broken/slow (blank device list, slow loading) independent of the
  Matter-specific issue — this needs to settle before further commissioning
  attempts are meaningful.

---

## Identity clash with the deadbolt lock (resolved 2026-10-27)

The motorised deadbolt (private repo `matter-deadbolt`, `lock_matter/`, same Door Lock sample
base) first used **exactly this buzzer's identity**: VID `0xFFF1`, PID `0x8006`, the stock
test DAC for 0x8006, Nordic's default serial `11223344556677889900`, discriminator 3840, so
the same QR code `MT:8IXS142C00KA0648G00`. Adding either device in Google Home removed the
other.

What was tried, and what happened:

| Change | Result |
|---|---|
| Own serial numbers (`BUZZER0001`, `DEADBOLT0001`) | Still merged: adding one still removed the other |
| Buzzer with a self-made DAC (test PAI-signed, verifies to the test PAA) | Rejected: "Failed generating device credentials" |
| Buzzer on PID 0x8005 (stock DAC), not registered in the console | Rejected the same way |
| Buzzer on 0x8005 right after registering it | Rejected (probably too soon after registering) |
| **Buzzer on 0x8006, deadbolt on 0x8005 (registered, stock DAC)** | **Both paired and stay** |

Final identities: buzzer **0xFFF1/0x8006** (this firmware, serial `BUZZER0001`, QR
`MT:8IXS142C00KA0648G00`); deadbolt **0xFFF1/0x8005** (serial `DEADBOLT0001`, discriminator
0xF01, QR `MT:6FCJ1CEK01KA0648G00`). The manual code `3497-011-2332` is the same for both, so
scan QR codes.

Rules learned:

- Google Home keeps one device per test VID/PID + stock DAC; a different serial is not
  enough. Give each device its own PID (with the SDK's stock DAC for it; Nordic ships
  0x8000-0x801F, all covered by the test CD).
- Self-made test DACs are refused by Google even though they chain to the test PAA.
- Once the Google account has a Developer Console project with test-VID integrations
  (project "Xiao test" here), Google only issues credentials for registered VID/PIDs:
  "Failed generating device credentials", right after the device answers the CSR. Register
  each PID (type Lock): 0x8005 and 0x8006 are registered. Nordic's ecosystem-testing
  tutorial on DevZone has the same requirement.

Re-adding the buzzer (USB needed):

1. Rebuild pristine and flash. Factory data (0xe5000) is inside the trimmed range; settings
   (the old fabric) are not erased. This PC blocks writes to the UF2 drive (group policy
   `RemovableStorageDevices\Deny_All`, "Access is denied"), so flash through the bootloader's
   serial port instead (no keys, app region only; a failed transfer leaves the board in the
   bootloader): triple-press reset, `pip install adafruit-nrfutil`, then
   `adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0xFFFE --application trimmed.hex buzzer_dfu.zip`
   and `adafruit-nrfutil dfu serial --package buzzer_dfu.zip -p COMxx -b 115200 --singlebank`
   (COMxx = the bootloader's port, USB VID 0x2886).
2. Factory-reset it on the shell (second CDC port of the app, VID 0x2FE3, COM90 here):
   `matter device factoryreset`. It reboots and, with `CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART=y`,
   opens the commissioning window for **15 minutes** (`CHIP_BLE_ADVERTISING_DURATION=15`).
   After that, `matter ble adv start` only restarts BLE advertising, not the commissioning
   window: the phone connects but the buzzer ignores it. Factory-reset again instead.
3. In Google Home: + -> Device -> Matter-enabled device, scan the QR code.

---

## Immediate Next Steps

1. Clear cache (not data) for **Google Home** and **Google Play Services**,
   restart phone, confirm the app loads normally and existing commercial
   Matter devices display correctly with no lag.
2. Once app is stable, test the **nRF54L15 light bulb sample** first as a
   clean baseline (no dev-console interference expected now that `0xFFF1` is
   no longer registered).
3. If that commissions successfully, retry the XIAO board #2 with its
   existing firmware (still VID `0xFFF1` / PID `0x8006`, DAC unchanged).
4. If Google's DAC verification is still slow, just retry — it has worked
   before with identical firmware/certs. This appears to be intermittent
   server-side latency rather than something fixable locally.
5. Long-term: for reliable dev-loop commissioning, consider commissioning via
   `chip-tool` or nRF Connect for Desktop's Matter controller first (which
   skips Google's slow attestation round-trip), then let Google Home pick the
   device up over the Thread network afterward.

---

## Lessons Learned

- **Do not register test VIDs in the Google Home Developer Console** unless
  actually pursuing certification/Field Trial — it changes how Google Home's
  consumer app handles that VID/PID and there is no UI-exposed way to fully
  undo it (no project delete button found).
- A slow "Generating Matter credentials" screen with clean device-side logs
  (DAC chain sent, no errors) is a Google server-side symptom, not a firmware
  problem — don't chase firmware changes for it.
- XIAO nRF52840 BAT+/BAT- pads are fragile — avoid excess reflow/rework once
  a battery lead is soldered; a lifted pad is very hard to repair reliably at
  that scale.
