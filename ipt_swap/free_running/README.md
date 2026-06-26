# ipt_swap / free_running — de-correlated free-running variant

This is an **alternative multi-link scheduler** for the role-swapped IPT Channel
Sounding sample. The parent [`../`](../README.md) uses a **round-robin turn token**
(serialized, reliable, ~7 Hz aggregate / ~2.3 Hz per anchor, 0 starvation). This
folder keeps CS **enabled continuously on every link** and instead **de-correlates
each link's CS-procedure cadence** so the links interleave on the radio instead of
one continuous link starving the rest.

Same role-swap rules as the parent: the **BLE central (reflector) drives all CS
setup**; each **peripheral-initiator owns procedure enable** and computes its own
IFFT distance via IPT. The only difference is *how the central shares the radio
across links*.

## Two apps

- `refl_central/` — BLE central + CS reflector. Drives CS setup per link and assigns
  each link a distinct CS-procedure interval (de-correlated cadence).
- `init_peripheral/` — BLE peripheral + CS initiator. Keeps CS enabled continuously at
  the central-assigned cadence; emits `DIST:<metres>,AP:0,SAMPLES:<tones>`.

## Round-robin vs free-running — the trade

| | Round-robin (parent `../`) | Free-running (this folder) |
| --- | --- | --- |
| Radio sharing | one link at a time (turn token) | all links continuous, de-correlated intervals |
| Aggregate rate | ~7 Hz | ~13–18 Hz |
| Per-anchor evenness | even, guaranteed turns | uneven |
| **Reliability** | **0 starvation across trials** | **30–50 % cold-start starvation of a weak link** |
| Mitigations | n/a | starvation self-heal watchdog + auto-reconnect supervisor |

**Use the round-robin parent for reliability; use this variant when you need the
higher aggregate rate and can tolerate occasional uneven/starved cadence on a weak
link.** The contention is fundamental: the SoftDevice Controller locks the radio to
one continuous CS link, so de-correlation reduces but does not eliminate starvation.

## Build & flash (NCS v3.3.1, `nrf54l15dk/nrf54l15/cpuapp`)

```
cd refl_central    && nwest build -b nrf54l15dk/nrf54l15/cpuapp -p always
cd ../init_peripheral && nwest build -b nrf54l15dk/nrf54l15/cpuapp -p always
flash <build-dir>   # one central-reflector + up to 3 peripheral-initiators
```
