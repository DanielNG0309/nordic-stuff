# nordic-stuff

My personal Nordic Semiconductor samples and projects — mostly **Bluetooth LE
Channel Sounding** (CS) ranging references for **NCS v3.3.1 / nRF54L15**.

> **DISCLAIMER:** the code is provided as is and is not to be considered official.

## Samples

| Folder | What it is |
| --- | --- |
| [`cs_ipt_multi/`](cs_ipt_multi) | Multi-connection CS using **Inline PCT Transfer (IPT)** — each initiator computes its own IFFT distance locally; round-robin so several initiators share one reflector. |
| [`cs_ras_multi/`](cs_ras_multi) | The **RAS** counterpart (Ranging Service step-data transfer + official `cs_de` IFFT), 2-connection round-robin. Practical to ~2–3 connections; for more, use the IPT approach. |
| [`ipt_swap/`](ipt_swap) | **Role-swapped** CS: the reflector is the BLE *central*, initiators are BLE *peripherals*. Round-robin turn-token scheduler. Includes a [`free_running/`](ipt_swap/free_running) variant (higher aggregate rate, less even). |
| [`scan_cs_soak/`](scan_cs_soak) | A **scan-while-CS gateway** reference: stay in Observer mode and do on-demand CS (connect → range → disconnect) forever without the scanner wedging or rebooting. Two architectures (continuous-scan vs Observer↔CS mode-switch), both CS roles. |
| [`flpr_sleep_test/`](flpr_sleep_test) | Multicore (FLPR) sleep/power benchmark. |

Each folder has its own README with topology, build/flash steps, and measured results.

## Build (NCS v3.3.1, `nrf54l15dk/nrf54l15/cpuapp`)

```
cd <sample>/<app> && west build -b nrf54l15dk/nrf54l15/cpuapp -p always
```
