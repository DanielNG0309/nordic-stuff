# cs_ras_multi - 2-connection RAS + IFFT round-robin Channel Sounding

A multi-connection Bluetooth LE **Channel Sounding** ranging reference on nRF54L15 that uses the
**Ranging Service (RAS)** to transfer step data and **Nordic's official `cs_de` IFFT** estimator to
compute distance. One reflector serializes ranging across several initiators with a **round-robin**,
so multiple initiators share one reflector without colliding - and it does so with **persistent
connections that are never torn down**, which is the key to avoiding the repeated-CS reset problem.

This is the RAS counterpart to the IPT solution in [`../cs_ipt_multi`](../cs_ipt_multi). RAS is
practical only to ~2-3 connections (see *Scaling limits* below); **for more anchors use the IPT
approach.** This sample is fixed at **2 initiators** on purpose.

## Topology

- **Reflector** (`projects/reflector_build`, advertises as `9999`) - the shared device (in a tracking
  deployment, the moving target). BLE peripheral + CS reflector. Hosts a small **sync** GATT service
  and runs a round-robin orchestrator.
- **Initiator** (`projects/initator_build`, name `0000`) - a ranging node (in a tracking deployment,
  a fixed anchor). BLE central + CS initiator. Connects to the reflector, and on each granted turn
  runs one CS procedure and computes an **IFFT distance locally** via `cs_de`. Flash the same image to
  every initiator.

## How the round-robin works

The reflector keeps all initiators connected and grants exactly one a turn at a time, so only one CS
procedure runs at any moment (independent initiators free-running CS would contend for the radio and
starve each other):

1. The reflector's `orchestration_thread` (`lib/ble/ble.c`) picks the next connected initiator and
   **indicates `1`** ("go") on the sync characteristic.
2. That initiator runs one CS procedure, computes its distance, and **writes `0`** ("done") back.
3. The reflector advances `turn_idx` to the next initiator and repeats.

Each initiator prints a parser-friendly distance line over its serial console:

```
DIST:<m>,AP:0,SAMPLES:<steps>,RSSI:<dBm>,RSSI_DIST:<m>
```


## Build & flash (NCS v3.3.1, `nrf54l15dk/nrf54l15/cpuapp`)

Each role is a separate build under `projects/`:

```
cd projects/reflector_build && west build -b nrf54l15dk/nrf54l15/cpuapp -p always   # flash to 1 DK
cd projects/initator_build  && west build -b nrf54l15dk/nrf54l15/cpuapp -p always   # flash to each initiator DK
```

Flash one DK with the reflector image and up to two with the initiator image. Read each initiator's
console for the `DIST:` lines.

## Scaling limits (why only 2)

RAS transfers per-procedure step data over a GATT service and reassembles it on the initiator, which
bounds how many connections one reflector can sustain. On nRF54L15 the controller's
`BT_CTLR_SDC_CS_COUNT` and `BT_CHANNEL_SOUNDING_REASSEMBLY_BUFFER_CNT` cap the practical count to ~2-3,
and RAM climbs quickly with connection count. This reference is therefore set to **2 initiators**
(`CONFIG_BT_MAX_CONN=2` on the reflector). For more anchors, the **IPT** approach in
[`../cs_ipt_multi`](../cs_ipt_multi) carries the reflector's measurement inside the returned tones
(no RAS GATT transfer), which scales further.

## Distance estimation

`lib/calc/calc.c` uses Nordic's official `cs_de` library (`CONFIG_CS_CALC=y` → selects `BT_CS_DE`,
FPU). It mirrors the parse-and-estimate logic of the official `ras_initiator` sample, feeding a small
sliding-window median (`DE_SLIDING_WINDOW_SIZE` in `lib/global.h`) to reject multipath/outliers. The
closed-source phase-slope library was removed - this build is IFFT-only.
