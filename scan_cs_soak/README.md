# scan_cs_soak — continuous-scan + on-demand Channel Sounding soak test


## The two architectures

| App | Scanner during CS | How |
| --- | --- | --- |
| `gateway/` | **never stops** | Parallel scan + initiate (`CONFIG_BT_SCAN_AND_INITIATE_IN_PARALLEL`, production since NCS v2.9.0). Connect/CS/disconnect happen *under* the running scanner. |
| `gateway_modeswitch/` | **stopped, then restarted** | The Observer↔CS *mode switch*: stop scanner → connect → CS → disconnect → **restart scanner**. The restart is the classic failure point for this pattern — done correctly here it recovers every cycle. |

Both avoid the `sys_reboot()` that the stock CS samples fall back to. 

## Topology (3 DKs, nRF54L15 DK)

| App | Role | Notes |
| --- | --- | --- |
| `gateway/` or `gateway_modeswitch/` | BLE **central**, CS initiator *or* reflector | the device under test |
| `target/` | BLE **peripheral**, CS reflector *or* initiator (opposite of the gateway) | advertises `CS Sample`, re-advertises after each disconnect |
| `beacon/` | constant non-connectable advertiser (`SOAK BEACON`) | scanner-health probe — its reports must keep arriving across CS cycles |

## CS role is selectable (and independent of BLE role)

The gateway is always the BLE **central** and always **drives the CS setup**
(capabilities, config, security) — that rule is non-negotiable. But the CS *role*
is a separate axis, chosen at compile time:

| Build | Gateway CS role | Target CS role | Who enables procedures |
| --- | --- | --- | --- |
| default | initiator | reflector | gateway |
| `-DCONFIG_APP_CS_ROLE_REFLECTOR=y` (gateway) + `-DCONFIG_APP_CS_ROLE_INITIATOR=y` (target) | reflector | initiator | target (peripheral-initiator) — the role-swap |

When the gateway is the reflector, the central still drives all setup and the
**peripheral becomes the CS initiator and enables the procedures** — the same
role-swap pattern as the `ipt_swap` sample, here with a single CS op per connection.

**Distance output.** Whichever device is the CS *initiator* computes the IFFT
distance locally — via **IPT** (`cs_enhancements_1 = 1`), so the reflector's phase
data rides inline and no RAS/GATT step transfer is needed — and prints
`DIST:<metres>,AP:0,SAMPLES:<tones>` each cycle (shared `common/cs_ifft.c`, Nordic's
`cs_de`). The heavy IFFT runs in thread context, never in the BT RX callback (doing
so stalls BT and aborts CS procedures).

## Key configuration

`gateway/prj.conf`:
- `CONFIG_BT_SCAN_AND_INITIATE_IN_PARALLEL=y` (+ controller option) — scan while connected.
- `CONFIG_BT_BONDABLE=n` — encrypt **fresh** each connection. CS security needs an
  encrypted link; storing bonds across rapid reconnects desyncs the keys between the
  two sides and encryption then fails with `err 4` (key missing).
- `CONFIG_THREAD_ANALYZER=y` + `CONFIG_INIT_STACKS=y` — print the MPSL Work watermark.

Teardown order that matters: disable CS procedures, **wait for the disable to
complete** (`le_cs_procedure_enable_complete`, state 0), *then* `bt_le_cs_remove_config`.
Removing the config before the disable lands returns `-13`.

## Build & flash (NCS v3.3.1, `nrf54l15dk/nrf54l15/cpuapp`)

```
# default (gateway = CS initiator, target = CS reflector)
cd gateway            && west build -b nrf54l15dk/nrf54l15/cpuapp -p always
cd ../target          && west build -b nrf54l15dk/nrf54l15/cpuapp -p always
cd ../beacon          && west build -b nrf54l15dk/nrf54l15/cpuapp -p always
cd ../gateway_modeswitch && west build -b nrf54l15dk/nrf54l15/cpuapp -p always

# role-swap (gateway = CS reflector, target = CS initiator) into separate build dirs
cd gateway  && west build -b nrf54l15dk/nrf54l15/cpuapp -p always -d build_refl -- -DCONFIG_APP_CS_ROLE_REFLECTOR=y
cd ../target && west build -b nrf54l15dk/nrf54l15/cpuapp -p always -d build_init -- -DCONFIG_APP_CS_ROLE_INITIATOR=y
```

Flash `gateway` (or `gateway_modeswitch`) + `target` + `beacon` to three DKs. Watch
the gateway console: every cycle prints `cycle N CS ok` + `remove_config ok`; a
`STATS` line every 5 s shows cycles, CS procedures, the running `scan_total`, the
per-window scan rate, and the thread analyzer.

## Result

- **Real distance every cycle** — the initiator prints e.g. `DIST:0.293,AP:0,SAMPLES:33`;
  gateway-initiator and target-initiator agree at the same physical distance.
- **CS runs every cycle** (`steps ≈ 42` per procedure) in all four combinations
  (continuous/mode-switch × initiator/reflector); `bt_le_cs_remove_config` succeeds.
- **The scanner never wedges.** Continuous: `scan_total` climbs steadily at
  ~220 reports/s. Mode-switch: it restarts cleanly every cycle (`scan_restarts` ==
  cycles, never a `RESTART FAILED`), at a lower ~90/s because it is blind during each
  CS op — the visible cost of stopping the scanner.
- **MPSL Work stack is flat** (`488/1024`) across all cycles — the "leak" is a watermark.
- Occasional single-cycle `CS FAILED` self-recovers; the loop never stalls.

**Takeaway:** keep scanning and do CS underneath it (continuous) for uninterrupted
coverage, or use the mode switch if you need CS to have the radio to itself — either
way, no reboot is required. Where the same peer is ranged repeatedly, keeping the
connection open and re-running procedures (no disconnect) is safer still.
