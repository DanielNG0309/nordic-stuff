# flpr_sleep_test

> **Disclaimer:** This document reflects my current understanding of the hardware and SDK
> behaviour. It may be incomplete or incorrect - treat it as a starting point for investigation
> rather than authoritative documentation.

Multicore benchmark measuring FLPR core sleep power consumption on nRF54H20, nRF54LM20A, and nRF54L15.

The APP core launches the FLPR core, configures RAM retention, then sleeps forever.
The FLPR core runs a compute workload, logs a counter, then enters a timed sleep - repeating indefinitely.
PM selects the deepest achievable sleep state based on the configured sleep duration and the
`min-residency-us` values in the board FLPR overlay.

---

## Supported Targets

| Board | Target |
|-------|--------|
| nRF54H20 DK | `nrf54h20dk/nrf54h20/cpuapp` |
| nRF54LM20A DK | `nrf54lm20dk/nrf54lm20a/cpuapp` |
| nRF54L15 DK | `nrf54l15dk/nrf54l15/cpuapp` |


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



