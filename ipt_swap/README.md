# ipt_swap — central-reflector Channel Sounding with IPT (role-swapped), multi-anchor

A Channel Sounding (CS) distance-ranging reference for **NCS v3.3.1 / nRF54L15** that
**swaps the usual BLE roles**: the CS **reflector is the BLE central** and the CS
**initiators are BLE peripherals**, using **Inline PCT Transfer (IPT)** so each initiator
computes its own IFFT distance locally (no RAS/GATT step-data transfer).

This fits a hub-and-anchors deployment: one central-reflector "tag" + N peripheral-initiator
"anchors", each anchor computing and reporting its own distance to the tag.

> **Two multi-link schedulers:** this top-level sample uses the **round-robin turn
> token** (reliable, even, ~7 Hz aggregate, 0 starvation — described below). An
> alternative **free-running, de-correlated** scheduler (higher aggregate ~13–18 Hz
> but with cold-start starvation on weak links) lives in
> [`free_running/`](free_running/README.md).

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
  `DIST:<metres>,AP:0,SAMPLES:<high-quality-tone-count>`.

## Multi-link fairness: round-robin turn token

Free-running concurrent CS on all links starves all but one (the SoftDevice Controller locks
the radio to one continuous CS link). Serialization fixes it. Because the swap puts the
turn-coordinator (central) and the procedure-enabler (initiator) on **different devices**, a
small **CS-Turn GATT service** carries the coordination:

- The central **writes `1` ("GO")** to one initiator's CS-Turn characteristic to grant a turn.
- That initiator enables a **bounded burst** of `CS_TURN_BURST` CS procedures, then disables and
  **notifies `1` ("DONE")**.
- The central advances to the next link. Only one link ranges at a time → no radio contention.

**Auto-reconnect** is folded into the same round-robin loop: an empty slot (a dropped/reset
anchor) is scanned and re-established instead of being granted a turn — collision-free because no
CS turn is active during the reconnect (single-threaded). A reset anchor self-heals back into the
ranging set in ~5 s, and the live anchors keep ranging meanwhile. Supervision timeout is 8 s.

## Measured (nRF54L15 DK)

| Config | Rate |
| --- | --- |
| Single link, continuous | ~66 Hz |
| 2 anchors, round-robin | ~2.3 Hz/anchor, balanced, 0 starvation |
| 3 anchors, round-robin | ~2.3 Hz/anchor (~6.8 Hz aggregate), **0 starvation across all trials** |

**Reliability:** round-robin's guaranteed turns mean every anchor ranges regardless of RF — 0 of
10+ cold-start trials starved a link (vs the free-running variant's 30–50 %). Per-anchor rate is
roughly constant whether 2 or 3 anchors (per-turn overhead dominates, not the anchor count). This
is ~10× a RAS or peripheral-reflector-IPT round-robin at 3 anchors. The rate does **not** respond
to connection-interval / channel-map / burst / steps tuning — the per-turn CS re-establishment is
a fixed cost (a re-enabled CS procedure runs ~6× slower than continuous single-link CS), so
~2.3–2.7 Hz/anchor is the practical ceiling.

## Tone filtering & accuracy

- **Tone Quality Indicator (TQI) filtering:** only `BT_HCI_LE_CS_TONE_QUALITY_HIGH` tones are fed to
  the IFFT, and a procedure is dropped unless at least `TONE_QI_OK_TONE_COUNT_THRESHOLD` (15) HIGH
  tones are present (matching the stock `ras_initiator`; the stock `ipt_initiator` does *not* filter).
  At close, clean line-of-sight every tone is HIGH so the filter is a no-op (`SAMPLES` stays ~32); it
  only removes tones at longer range / multipath.

- **`CONFIG_APP_CS_DISTANCE_OFFSET_MM`** subtracts a fixed offset from each estimate (before the
  median filter) to compensate. Because the bias is distance-dependent, pick the offset for your
  operating range: 800 mm is spot-on at ~1 m; 500 mm (the default here) gives ~±0.1 m across 1–2 m;
  ~450 mm suits ~2 m. Re-measure at known distances to calibrate for a given deployment.

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
