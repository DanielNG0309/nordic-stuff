# ipt_swap — central-reflector Channel Sounding with IPT (role-swapped), multi-anchor

A Channel Sounding (CS) distance-ranging reference for **NCS v3.3.1 / nRF54L15** that
**swaps the usual BLE roles**: the CS **reflector is the BLE central** and the CS
**initiators are BLE peripherals**, using **Inline PCT Transfer (IPT)** so each initiator
computes its own IFFT distance locally (no RAS/GATT step-data transfer).

This fits a hub-and-anchors deployment: one central-reflector "tag" + N peripheral-initiator
"anchors", each anchor computing and reporting its own distance to the tag.

## Why role-swap + IPT

- **IPT** removes the per-procedure RAS GATT transfer (the bottleneck in RAS-based ranging).
- The **central** can schedule/space its links (ACL event spacing, uniform interval), which a
  peripheral-reflector structurally cannot — this is what makes fair multi-link CS possible.

The key rule that makes the swap work: **the BLE central must drive the entire CS setup**
(ACL security → CS capability exchange → CS config create with `cs_enhancements_1=1` for IPT,
`role=REFLECTOR` → CS security enable). The **peripheral-initiator** owns procedure enable.
If the peripheral drives setup you get IPT config status `0x11` / CS security `err -13`.

## Two apps

- `refl_central/` — BLE central + CS **reflector**. Scans for the initiators, drives CS setup
  for each link, and runs the round-robin turn scheduler.
- `init_peripheral/` — BLE peripheral + CS **initiator**. Advertises as `Nordic CS IPT
  Initiator`, computes IFFT distance from its own subevent data, and emits a parser line:
  `DIST:%.3f,AP:0,SAMPLES:%d,RSSI:%d,RSSI_DIST:%.3f`.

## Multi-link fairness: round-robin turn token

Free-running concurrent CS on all links starves all but one (the SoftDevice Controller locks
the radio to one continuous CS link). Serialization fixes it. Because the swap puts the
turn-coordinator (central) and the procedure-enabler (initiator) on **different devices**, a
small **CS-Turn GATT service** carries the coordination:

- The central **writes `1` ("GO")** to one initiator's CS-Turn characteristic to grant a turn.
- That initiator enables a **bounded burst** of `CS_TURN_BURST` CS procedures, then disables and
  **notifies `1` ("DONE")**.
- The central advances to the next link. Only one link ranges at a time → no radio contention.

## Measured (nRF54L15 DK)

| Config | Rate |
| --- | --- |
| Single link, continuous | ~66 Hz |
| 2 anchors, round-robin | ~1.75 Hz/anchor, balanced, 0 drops |
| 3 anchors, round-robin | ~1.7–2.8 Hz/anchor (~7 Hz aggregate), stable |

Per-anchor rate is roughly constant whether 2 or 3 anchors (the per-turn CS re-enable + GATT
handshake overhead dominates). This is ~10× a RAS or peripheral-reflector-IPT round-robin at 3
anchors. Accuracy carries the inherent `cs_de` IFFT offset (~+0.5–0.8 m at ~2 m), same as the
stock samples.

## Implementation notes / gotchas

- **`CONFIG_BT_MAX_PAIRED` must be ≥ `CONFIG_BT_MAX_CONN`** on the central — the `bt_keys` pool
  is sized by it regardless of bonding, else the 2nd link's pairing fails with `-ENOMEM`.
- **Uniform connection interval** across links (not de-correlated): with round-robin there is no
  contention to de-correlate, and distinct intervals make the link anchors drift so an active
  link's 5 ms CS subevent periodically clobbers another link's ACL event and starves it.
- **Interval must fit the CS subevent**: a ~5 ms CS subevent + N link anchors (spaced ~6.5 ms)
  needs ≥ ~30 ms for 3 links; 20 ms over-subscribed the schedule and one link's CS never ran.
- **CS-Turn handles are used directly** (char value / CCC) — the 128-bit-UUID-filtered GATT
  discovery proved unreliable here; a production build should discover via `bt_gatt_dm`.
- Subscribe to notifications in **thread context**, not inside the discovery callback (the ATT
  channel is still busy and the CCC write is dropped).

## Build & flash

```
cd refl_central   && nwest build -b nrf54l15dk/nrf54l15/cpuapp -p always
cd init_peripheral && nwest build -b nrf54l15dk/nrf54l15/cpuapp -p always
flash <build-dir>   # one central-reflector + up to 3 peripheral-initiators
```
