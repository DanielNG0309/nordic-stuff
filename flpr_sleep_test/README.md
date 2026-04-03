# flpr_sleep_test

> **Disclaimer:** This document reflects my current understanding of the hardware and SDK
> behaviour. It may be incomplete or incorrect — treat it as a starting point for investigation
> rather than authoritative documentation.

Multicore benchmark measuring FLPR core sleep power consumption on nRF54H20, nRF54LM20A, and nRF54L15.

The APP core launches the FLPR core, configures RAM retention, then sleeps forever.
The FLPR core runs a compute workload, logs a counter, then enters a timed sleep — repeating indefinitely.
PM selects the deepest achievable sleep state based on the configured sleep duration and the
`min-residency-us` values in the board FLPR overlay.

---

## Supported Targets

| Board | Target |
|-------|--------|
| nRF54H20 DK | `nrf54h20dk/nrf54h20/cpuapp` |
| nRF54LM20A DK | `nrf54lm20dk/nrf54lm20a/cpuapp` |
| nRF54L15 DK | `nrf54l15dk/nrf54l15/cpuapp` |

---

## Directory Structure

```
flpr_sleep_test/
├── CMakeLists.txt          # App core image
├── Kconfig                 # Shared Kconfig (both images)
├── prj.conf                # App — logging enabled (console tests)
├── prj_silent.conf         # App — logging disabled (PPK2 power measurements)
├── sysbuild.cmake          # Launches remote_flpr image
├── sysbuild.conf
├── testcase.yaml           # Twister test definitions
├── boards/
│   ├── nrf54h20dk_nrf54h20_cpuapp.overlay
│   ├── nrf54lm20dk_nrf54lm20a_cpuapp.overlay
│   └── nrf54l15dk_nrf54l15_cpuapp.overlay
├── src/
│   └── main_app.c          # APP core: RAM retention setup, sleep forever
└── remote/
    ├── CMakeLists.txt      # FLPR core image
    ├── Kconfig
    ├── prj.conf            # FLPR — logging enabled
    ├── prj_silent.conf     # FLPR — logging disabled + tickless
    ├── boards/
    │   ├── nrf54h20dk_nrf54h20_cpuflpr.overlay
    │   ├── nrf54lm20dk_nrf54lm20a_cpuflpr.overlay
    │   └── nrf54l15dk_nrf54l15_cpuflpr.overlay
    └── src/
        ├── main_flpr.c     # FLPR core: compute workload + k_msleep
        └── power_off.c     # Zephyr PM hooks — maps substates to VPR sleep CSR writes
```

---

## Build

Test cases are defined in `testcase.yaml`. Example:

```bash
west build -b nrf54lm20dk/nrf54lm20a/cpuapp --sysbuild -T benchmarks.multicore.flpr_sleep_test.lm20_hibernate .
```
Or just add the extra CMAKE args manually

---

## Kconfig Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_TEST_SLEEP_DURATION_MS` | 100 | FLPR sleep duration in ms. PM selects the deepest state whose `min-residency-us` is satisfied. |
| `CONFIG_TEST_BASELINE_SLEEP` | n | Both cores sleep forever with no compute and no GRTC wakeup. Used to measure the true system current floor. |

---

## Sleep States

### nRF54H20 FLPR (2 states)

| Substate | Name | min-residency |
|----------|------|---------------|
| 0 | wait (standby) | 20 ms |
| 0 | hibernate (suspend-to-ram) | 400 ms |

### nRF54LM20A / nRF54L15 FLPR (4 states)

| Substate | Name | min-residency |
|----------|------|---------------|
| 0 | wait | 20 ms |
| 1 | sleep | 400 ms |
| 2 | deep sleep | 600 ms |
| 3 | hibernate | 1000 ms |

---

## Known Issues / Open Items

### 1. Periodic wakeup sawtooth during sleep (LM20A + L15)

**Symptom:** During sleep, a sawtooth waveform of ~6–12 µA appears with a period of ~7 ms,
resulting in a sleep average of about 3.5 µA.

**Investigation so far:**
- `CONFIG_TICKLESS_KERNEL=y` was added to both app and remote `prj_silent.conf` — no effect on
  the sawtooth period or amplitude. The tick interrupt is not the cause.
- `CONFIG_NRF_GRTC_TIMER_AUTO_KEEP_ALIVE=y` is present in the built config and is a candidate —
  it periodically wakes the system to service the GRTC SYSCOUNTER. However this cannot be
  directly disabled (no user-assignable Kconfig prompt) and has not been confirmed as the root cause.
- Root cause is not yet confirmed.

---

### 2. Hibernate wakeup — no explicit VPR reset required (LM20A + L15)

**Background:** The datasheet describes hibernate as a state where the FLPR powers off and saves
its register context to SRAM. On wakeup the FLPR is expected to reset and restore context from
that save area, with the APP core potentially needing to explicitly trigger a reset.

**Observed behavior:** On both nRF54LM20A (engA + engB) and nRF54L15, the FLPR resumes
correctly after the configured hibernate sleep duration an explicit VPR reset sequence. 
The FLPR continues its compute/sleep loop as expected across multiple cycles.

**Note:** Given the sawtooth wakeup issue described in issue 1, it is uncertain whether the FLPR
is truly entering full hibernate (power off, context saved) or a lighter sleep state. The PM
subsystem selects the state based on `min-residency-us` thresholds, but actual hardware behavior
has not been independently confirmed (e.g. via logic analyzer on power domain rails). If the FLPR
is not genuinely hibernating, the absence of a required reset call would be expected.
