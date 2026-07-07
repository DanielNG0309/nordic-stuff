# cs_ipt_multi - Multiconnection Channel Sounding (IPT) distance ranging

Multi-anchor Bluetooth LE **Channel Sounding** ranging on nRF54L15, using Nordic's
**CS IPT** (Inline PCT Transfer) so each anchor computes an **IFFT** distance estimate from its
own subevent data - no Ranging Service (RAS) GATT transfer. Built for an indoor moving-target
use case: several fixed anchors locate one moving target.

## Topology

- **`target_reflector/`** - the moving **target**: BLE peripheral + CS reflector. Advertises as
  `Nordic CS IPT Reflector`, accepts up to `CONFIG_BT_MAX_CONN` anchors, and hosts a small
  **"CS-turn"** GATT service used to grant ranging turns round-robin.
- **`anchor_initiator/`** - a fixed **anchor**: BLE central + CS initiator. Connects to the target,
  subscribes to the CS-turn service, and on each granted turn runs one bounded CS procedure and
  computes/reports an IFFT distance locally (`cs_de_ifft`). Flash the same image to every anchor.

## Why round-robin

Independent anchors free-running CS concurrently contend for the radio and starve each other. The
reflector therefore **serializes** ranging: it notifies one anchor "go", waits for its "done", then
advances. This gives fair multi-anchor ranging with full IFFT tone counts, while keeping IPT's
no-RAS, low-latency local computation. The anchor is a **reconnect state machine** (no reboot on
disconnect; bounded waits; pairing retry) so anchors self-heal from churn.

## Output

Each anchor logs the stock `Distance estimates:` line plus a tool-compatible line:
`DIST:<m>,AP:0,SAMPLES:<tones>,RSSI:<dBm>,RSSI_DIST:<m>` (IFFT median as DIST).

## Build & flash (NCS v3.3.1, nrf54l15dk/nrf54l15/cpuapp)

```
west build -b nrf54l15dk/nrf54l15/cpuapp -p always    # in each app dir
```
Flash one DK with `target_reflector`, the rest with `anchor_initiator`. (USB/CS notes are
environment-specific.)

## Status

Round-robin serialization + IFFT proven on 4 DKs (1 target + 3 anchors); anchor reconnect-hardening
in progress. See commit history for details.
