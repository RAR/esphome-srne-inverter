# esphome-srne-inverter

ESPHome external component for SRNE hybrid solar inverters (and OEM rebadges like the [Anenji 12KW 48V split-phase 120/240V](https://anenji.com/products/anenji-12kw-48v-hybrid-solar-inverter-split-phase-120v-240v-dual-mppt-charger-parallel-6-units)) via RS485 / Modbus RTU.

Beyond plain read-only telemetry it adds:

- **Split-phase / parallel-120 awareness** — L1 + L2 broken out separately *and* combined; line-to-line voltage is computed correctly for the inverter's actual `AC Output Phase Mode` setting
- **Writable settings via Modbus 0x06** — output priority, charge priority, battery type, charge currents, output voltage, eco mode, auto-restart switches, SOC thresholds, …
- **Symbolic fault codes** — names from the user manual instead of raw numbers
- **One-shot register-space scan mode** for discovering registers undocumented in the official PDF

Designed and developed against an **Anenji 12KW (SRNE SN ANJ2602260356-100132, firmware CPU1 v9.00 / CPU2 v1.04)** running in split-phase 120/240V mode. Other SRNE models / firmware revs should mostly work — diff any unfamiliar register behaviour by re-running the scan mode (see below).

## Quickstart

1. Wire your ESP32 to the inverter's WIFI or RS485 jack — see [WIRING.md](WIRING.md). On the Anenji 12KW the WIFI RJ45 jack is pin 7 = A, pin 8 = B, pin 2 = GND.
2. Copy `secrets.yaml.example` to `secrets.yaml` and fill in your Wi-Fi credentials (plus API encryption key, OTA password, fallback AP password if using `atoms3r-example.yaml`).
3. Pick a starting-point YAML:
   - `esp32-example.yaml` — generic ESP32 DevKit (Arduino)
   - `esp32s3-example.yaml` — generic ESP32-S3 (ESP-IDF)
   - `atoms3r-example.yaml` — M5Stack AtomS3R with the LCD live-status display
4. `esphome run <chosen>.yaml`.

## Live snapshot — every supported entity

Values below are real readings from the Anenji 12KW at runtime (battery 86% SOC, no grid connected, no PV connected, inverter in soft-start standby).

Entities marked *diagnostic* are placed under the **Diagnostic** section of the HA device card by `atoms3r-example.yaml` (HVDC link, heatsink temps, fault/state/version/serial — see [`atoms3r-example.yaml`](atoms3r-example.yaml) for the exact `entity_category` settings). The display names shown in HA may differ from the schema keys; both are noted where they diverge.

### Sensors

| Entity | Live value | Register | Notes |
|---|---|---|---|
| `battery_soc` | `86 %` | `0x0100` | |
| `battery_voltage` | `53.4 V` | `0x0101` | |
| `battery_current` | `1.7 A` | `0x0102` | signed; negative = discharging. `atoms3r-example.yaml` exposes this as **"Battery Discharge Current"** |
| `battery_charge_current` | `0.0 A` | `0x021E` | mains-side charge current |
| `pv_charge_current` | `0.0 A` | `0x0224` | PV-side charge current |
| `charge_power` | `0 W` | `0x010E` | mains + PV combined |
| `pv1_voltage / current / power` | `0 V / 0 A / 0 W` | `0x0107`–`0x0109` | |
| `pv2_voltage / current / power` | `0 V / 0 A / 0 W` | `0x010F`–`0x0111` | |
| `pv_total_power` | `0 W` | derived | `pv1_power + pv2_power` |
| `bus_voltage` | `527.9 V` | `0x0212` | total HVDC link — *diagnostic* |
| `dc_bus_positive_voltage` | `263.9 V` | `0x0228` | "DC Bus +" — undocumented in PDF, found via scan — *diagnostic* |
| `dc_bus_negative_voltage` | `264.0 V` | `0x0229` | "DC Bus -" — undocumented; +/-264V sums ≈ `bus_voltage` — *diagnostic* |
| `grid_voltage / current / frequency` | `0 / 0 / 0` | `0x0213`–`0x0215` | (grid not connected) |
| `inverter_voltage` | `120.5 V` | `0x0216` | L1 (display name "Inverter Voltage L1") |
| `inverter_current` | `1.3 A` | `0x0217` | L1 (display name "Inverter Current L1") |
| `inverter_frequency` | `59.99 Hz` | `0x0218` | |
| `inverter_voltage_l2` | `120.4 V` | `0x022C` | L2 (split-phase / parallel) |
| `inverter_current_l2` | `1.2 A` | `0x022E` | L2 |
| `inverter_voltage_l1_l2` | `240.9 V` | derived | **mode-aware**: sum in split, `\|L1-L2\|` in parallel |
| `inverter_current_total` | `2.5 A` | derived | L1 + L2 |
| `load_current` | `0.4 A` | `0x0219` | L1 (display name "Load Current L1") |
| `load_active_power` | `0 W` | `0x021B` | L1 (display name "Load Power L1") |
| `load_apparent_power` | `57 VA` | `0x021C` | L1 (display name "Load Apparent Power L1") |
| `load_percent` | `0 %` | `0x021F` | L1 (display name "Load Percent L1") |
| `load_current_l2` | `0.0 A` | `0x0230` | |
| `load_active_power_l2` | `3 W` | `0x0232` | |
| `load_apparent_power_l2` | `9 VA` | `0x0234` | |
| `load_percent_l2` | `0 %` | `0x0236` | |
| `load_current_total` | `0.4 A` | derived | L1 + L2 |
| `load_active_power_total` | `3 W` | derived | total real power |
| `load_apparent_power_total` | `66 VA` | derived | total apparent |
| `heatsink_a_temperature` | `31.4 °C (88.5 °F)` | `0x0220` | DC-DC — *diagnostic* |
| `heatsink_b_temperature` | `43.3 °C (109.9 °F)` | `0x0221` | DC-AC — *diagnostic* |
| `heatsink_c_temperature` | `57.8 °C (136.0 °F)` | `0x0222` | transformer — *diagnostic* |

### Binary sensors

| Entity | Live value | Source |
|---|---|---|
| `online_status` | `On` | watchdog on Modbus responses |
| `inverter_on_load` | `Off` | `machine_state` == 5 (Inverter powered) or 7 (Mains→Inverter). True only when the inverter is actively driving the load (UPS "on-load" sense); false in mains bypass, soft-start/standby, shutdown, or fault. |
| `grid_present` | `Off` | `grid_voltage > 50 V` |
| `fault` | `Off` | any of `0x0200`–`0x0203` ≠ 0 (block C not exposed on this firmware) |
| `split_phase_mode` | `On` | `0xE21E == 2` (= 180° split); `Off` = parallel/0° — *diagnostic* |

### Text sensors

| Entity | Live value | Source |
|---|---|---|
| `machine_state` | `Soft start` | `0x0210` decoded (11 known states) — *diagnostic* |
| `charge_state` | `Off` | `0x010B` decoded |
| `fault_codes` | `None` | `0x0204`–`0x0207` decoded to symbolic names per the manual (`BatVoltLow`, `OverloadInverter`, `ParaAcSrcDiff`, …) — also includes `BMSChargeDisabled` for fault code 59 (undocumented in §7.1; observed when BMS reports 100% SOC — BMS-side charge cutoff). `None` when no active faults. *diagnostic* |
| `software_version` | `CPU1 v9.00 / CPU2 v1.04` | `0x0014`–`0x0015` — *diagnostic* |
| `hardware_version` | `Control v3.00 / Power v3.04` | `0x0016`–`0x0017` — *diagnostic* |
| `serial_number` | `ANJ2602260356-100132` | `0x0035`–`0x0048` (ASCII, low byte per reg) — *diagnostic* |
| `battery_type` | `LFP L14 (raw 0x06)` | `0xE004` decoded — read-only mirror; the writable select is firmware-locked on Anenji (see [Known firmware locks](#known-firmware-locks)). *diagnostic* |

### Writable settings (Modbus function `0x06`)

All marked as Configuration entities in HA. Changing one in HA writes immediately; the new value is re-read on the next slow-poll cycle (~5 min) to confirm.

| Entity | Live value | Register | Range |
|---|---|---|---|
| `select.output_priority` | `SBU` | `0xE204` | Solar / Line / SBU |
| `select.charge_priority` | `Hybrid` | `0xE20F` | PV preferred / Mains preferred / Hybrid / PV only |
| `select.ac_input_voltage_range` | `APL` | `0xE20B` | UPS (120/110 V, 90-140 V) / APL (100/105 V, 85-140 V) |
| `select.parallel_mode` | `SIG (single)` | `0xE201` | SIG / PAL / 2P0-2P2 / 3P1-3P3 — **only writable while inverter is shut down** (runtime writes return error 0x08) |
| `number.output_voltage` | `120.0 V` | `0xE208` | 100-264 V |
| `number.max_charge_current` | (slow-poll pending) | `0xE20A` | 0-200 A (manual §5.2 item 07 "Battery charge current") |
| `number.mains_charge_current_limit` | (slow-poll pending) | `0xE205` | 0-100 A (manual §5.2 item 28 says 0-120, but Anenji firmware empirically rejects >100) |
| `number.soc_discharge_alarm` | (slow-poll pending) | `0xE01E` | 0-100 % — *register tentative* |
| `number.soc_discharge_cutoff` | (slow-poll pending) | `0xE00F` | 0-100 % — *register tentative* |
| `number.soc_charge_cutoff` | (slow-poll pending) | `0xE01D` | 0-100 % — *register tentative* |
| `number.soc_switch_to_mains` | (slow-poll pending) | `0xE01F` | 0-100 % — *register tentative* |
| `number.soc_switch_to_inverter` | (slow-poll pending) | `0xE020` | 0-100 % — *register tentative* |
| `switch.eco_mode` | (slow-poll pending) | `0xE20C` | turn off to take inverter out of standby |
| `switch.overload_auto_restart` | (slow-poll pending) | `0xE20D` | |
| `switch.overheat_auto_restart` | (slow-poll pending) | `0xE20E` | |
| `switch.buzzer_alarm` | (slow-poll pending) | `0xE210` | |
| `switch.inverter_to_bypass` | (slow-poll pending) | `0xE212` | |

SOC thresholds only take effect when BMS communication is up (menu items 32 = `485` and 33 = matching protocol). The register addresses for the SOC settings were calibrated from scan defaults — verify against your firmware before trusting; the manual doesn't publish register addresses.

### Known firmware locks

Verified empirically on the Anenji 12KW (CPU1 v9.00 / CPU2 v1.04) — the component now decodes Modbus exception codes and logs the failing register, so you'll see the failure mode in the log even if a write looks like it "worked" optimistically:

| Register | Behaviour | Mitigation |
|---|---|---|
| `0xE004` battery_type | **Permission denied (`0x0B`)** at all times — Anenji firmware locks this register against Modbus writes to prevent setting a destructive value. | `atoms3r-example.yaml` exposes a read-only `text_sensor.battery_type` mirror; change via the keypad (menu item 08). The writable `select.battery_type` schema option is kept for non-Anenji variants. |
| `0xE201` parallel_mode | **Cannot be changed while running (`0x08`)** — works only when the inverter is in the `Shutdown by user` state. | Shut down the inverter, change in HA, restart. |

If your firmware variant accepts writes to `0xE004`, swap `text_sensor.battery_type` back to `select.battery_type` in your YAML — the select implementation is still wired up.

## Protocol

- 9600 8N1, Modbus RTU, standard CRC16 (poly `0xA001`)
- Read: function `0x03`, max **20 registers per request** on this firmware (the V1.7 PDF claims 32 but the inverter silently times out beyond 20)
- Write: function `0x06` (single register)
- 50 ms inter-request quiet period after every response — some firmware drops requests that arrive too soon after the previous reply
- The component pipelines requests; if a request times out the device callback is notified so the per-device queue stays in sync with the response stream

Full register decoding details and block boundaries: [REGISTER_MAP.md](REGISTER_MAP.md).

## Diagnostic scan mode

If your firmware reports values at different addresses or you want to discover something not documented, flash `atoms3r-scan.yaml` temporarily:

```bash
esphome run atoms3r-scan.yaml
esphome logs atoms3r-scan.yaml > scan.log
# wait for "SCAN COMPLETE: N/M registers responded"
esphome run atoms3r-example.yaml   # back to normal
```

The scan sweeps ~350 registers across P00 / P01 / P02 / P05 / P07 / P08 and logs each as `SCAN 0xXXXX: u16=… i16=… hex=… ascii=…`, plus `SCAN 0xXXXX: ERROR 0x02` for ones the firmware doesn't expose. Diffing two scans before/after toggling a setting on the inverter's keypad identifies the corresponding Modbus register — that's how the L2 split-phase block, the AC Output Phase Mode register, and the DC bus rail addresses were all found.

## Not yet supported

Easy additions — open a request:

- `select.battery_charge_mode` (menu item 06 SNU/OSO — register address unknown, needs a scan-diff probe)
- Individual battery voltage thresholds (menu items 04, 05, 09-15, 17, 35, 37) — registers known per V1.7 PDF; intentionally not exposed since wrong values can damage batteries
- 3-phase B/C registers — schema is there but currently the L2 sensors decode only the "phase B" of split / parallel-120 mode
- Time-of-use schedule (menu items 40-45 charge, 47-52 discharge) — registers `0xE026`-`0xE032`
- Power switch (`0xDF00`) — deliberately omitted because it would also kill the ESP if the inverter powers it

## License

Same as the sibling [esphome-eg4-bms](https://github.com/RAR/esphome-eg4-bms) project.
