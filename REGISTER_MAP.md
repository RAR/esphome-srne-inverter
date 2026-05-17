# SRNE Inverter — registers decoded by this component

Subset of the V1.7 register map (`srne-hybrid-solar-inverter-modbus-protocol-v1-7.pdf`). Multipliers below give the conversion to physical units: `physical = raw * multiplier`.

## Block A — controller / PV (`0x0100`–`0x0111`, 18 regs, polled every cycle)

| Reg | Name | Type | Mult | Unit | Sensor |
|---|---|---|---|---|---|
| 0x0100 | Battery SOC | u16 | 1 | % | `battery_soc` |
| 0x0101 | Battery voltage | u16 | 0.1 | V | `battery_voltage` |
| 0x0102 | Battery current | i16 | 0.1 | A | `battery_current` (signed; negative = discharge) |
| 0x0107 | PV1 voltage | u16 | 0.1 | V | `pv1_voltage` |
| 0x0108 | PV1 current | u16 | 0.1 | A | `pv1_current` |
| 0x0109 | PV1 power | u16 | 1 | W | `pv1_power` |
| 0x010B | Charge state | u16 | — | enum | `charge_state` (text) |
| 0x010E | Charge power | u16 | 1 | W | `charge_power` (total) |
| 0x010F | PV2 voltage | u16 | 0.1 | V | `pv2_voltage` |
| 0x0110 | PV2 current | u16 | 0.1 | A | `pv2_current` |
| 0x0111 | PV2 power | u16 | 1 | W | `pv2_power` |
| — | (derived) | — | — | W | `pv_total_power` = pv1_power + pv2_power |

## Block B — inverter (polled every cycle, split into three reads)

The password-protection status register at `0x0211` silently times out on this SRNE firmware variant when read inside a multi-register span (no response, not a Modbus error). `0x0210` (machine state) reads fine as a 1-reg read with a 50ms inter-request quiet period in front of it, so we poll it on its own.

### Block B0 — machine state (`0x0210`, 1 reg)

| Reg | Name | Type | Mult | Unit | Sensor |
|---|---|---|---|---|---|
| 0x0210 | Machine state | u16 | — | enum | `machine_state` (text), `inverter_on` (binary) |

### Block B1 — bus/grid/inverter/load (`0x0212`–`0x021F`, 14 regs)

| Reg | Name | Type | Mult | Unit | Sensor |
|---|---|---|---|---|---|
| 0x0212 | Bus voltage | u16 | 0.1 | V | `bus_voltage` |
| 0x0213 | Grid voltage A | u16 | 0.1 | V | `grid_voltage`, `grid_present` (binary, > 50 V) |
| 0x0214 | Grid current A | u16 | 0.1 | A | `grid_current` |
| 0x0215 | Grid frequency | u16 | 0.01 | Hz | `grid_frequency` |
| 0x0216 | Inverter voltage A | u16 | 0.1 | V | `inverter_voltage` |
| 0x0217 | Inverter current A | u16 | 0.1 | A | `inverter_current` |
| 0x0218 | Inverter frequency | u16 | 0.01 | Hz | `inverter_frequency` |
| 0x0219 | Load current A | u16 | 0.1 | A | `load_current` |
| 0x021B | Load active power A | u16 | 1 | W | `load_active_power` |
| 0x021C | Load apparent power A | u16 | 1 | VA | `load_apparent_power` |
| 0x021E | Battery charge current (mains) | u16 | 0.1 | A | `battery_charge_current` |
| 0x021F | Load percent | u16 | 1 | % | `load_percent` |

### Block B3 — L2 leg data (`0x022A`–`0x0236`, 13 regs, polled only if any L2 sensor is configured)

Populates only when the inverter is actively outputting on both legs (i.e. not in eco standby). On this firmware variant the V1.7 PDF's "specific machine models" warning for `0x022A`–`0x0237` translates to "split-phase / parallel-120 second leg" data. Phase C slots (`0x022B`, `0x022D`, `0x022F`, `0x0231`, `0x0233`, `0x0235`) are 3-phase and stay 0; we just skip them on decode.

| Reg | Name | Type | Mult | Unit | Sensor |
|---|---|---|---|---|---|
| 0x022A | Grid voltage L2 | u16 | 0.1 | V | `grid_voltage_l2` |
| 0x022C | Inverter voltage L2 | u16 | 0.1 | V | `inverter_voltage_l2` |
| 0x022E | Inverter current L2 | u16 | 0.1 | A | `inverter_current_l2` |
| 0x0230 | Load current L2 | u16 | 0.1 | A | `load_current_l2` |
| 0x0232 | Load active power L2 | u16 | 1 | W | `load_active_power_l2` |
| 0x0234 | Load apparent power L2 | u16 | 1 | VA | `load_apparent_power_l2` |
| 0x0236 | Load percent L2 | u16 | 1 | % | `load_percent_l2` |

### Block B2 — heatsinks + PV charge + DC bus rails (`0x0220`–`0x0229`, 10 regs)

| Reg | Name | Type | Mult | Unit | Sensor |
|---|---|---|---|---|---|
| 0x0220 | Heat sink A (DC-DC) | i16 | 0.1 | °C | `heatsink_a_temperature` |
| 0x0221 | Heat sink B (DC-AC) | i16 | 0.1 | °C | `heatsink_b_temperature` |
| 0x0222 | Heat sink C (transformer) | i16 | 0.1 | °C | `heatsink_c_temperature` |
| 0x0224 | PV charge current | u16 | 0.1 | A | `pv_charge_current` |
| 0x0228 | +DC bus rail voltage | u16 | 0.1 | V | `dc_bus_positive_voltage` (undocumented — discovered via register scan) |
| 0x0229 | -DC bus rail voltage | u16 | 0.1 | V | `dc_bus_negative_voltage` (undocumented — sum ≈ `bus_voltage` at 0x0212) |

## Block C — faults (`0x0200`–`0x0207`, 8 regs, polled only if a fault sensor is configured)

The PDF tags this register range as "used by the internal debug tool". On at least some SRNE firmware versions the range is not exposed (silent timeout, no Modbus error). To avoid noise we only poll Block C when the user has configured `fault` or `fault_codes` — same for Block D (versions) and Block E (serial), which are only polled when their text sensors are configured.

| Reg | Name | Sensor |
|---|---|---|
| 0x0200–0x0203 | Current fault bits (64 bits across 4 regs) | `fault` (binary, true if any non-zero) |
| 0x0204–0x0207 | Up to 4 active fault codes | `fault_codes` (text, codes joined with `;`, "None" if all zero) |

## Block D — versions (`0x0014`–`0x0017`, 4 regs, polled every 30 cycles)

| Reg | Name | Sensor |
|---|---|---|
| 0x0014 | CPU1 software version | part of `software_version` text |
| 0x0015 | CPU2 software version | part of `software_version` text |
| 0x0016 | Control board hardware version | part of `hardware_version` text |
| 0x0017 | Power board hardware version | part of `hardware_version` text |

Format: raw value 100 → "v1.00", 145 → "v1.45".

## Block E — serial number (`0x0035`–`0x0048`, 20 regs, polled every 30 cycles)

ASCII string, low byte of each register is valid (high byte is padding). Trimmed of trailing spaces/nulls. Sensor: `serial_number`.

## Blocks F1 / F2 / F3 — writable settings (polled every 30 cycles, only if any setting entity is configured)

Block F* registers back the current value of any writable setting the user exposed via `select:` or `number:`. Writes go out via Modbus function `0x06` (write single register). Settings are re-read every ~5 min — if you change a setting in HA, the UI updates optimistically and the next F-block read confirms.

| Reg | Name | Block | Type | Mult | Range | Entity |
|---|---|---|---|---|---|---|
| 0xE204 | Output priority | F1 | enum | — | 0 Solar / 1 Line / 2 SBU | `select.output_priority` |
| 0xE205 | Mains charge current limit | F1 | u16 | 0.1 | 0–100 A | `number.mains_charge_current_limit` |
| 0xE208 | Output voltage | F2 | u16 | 0.1 | 100–264 V | `number.output_voltage` |
| 0xE20A | Maximum charge current | F2 | u16 | 0.1 | 0–150 A | `number.max_charge_current` |
| 0xE20F | Charge priority | F3 | enum | — | 0 PV pref / 1 Mains pref / 2 Hybrid / 3 PV only | `select.charge_priority` |

The F-blocks are split (F1 = `0xE204..E205`, F2 = `0xE208..E20A`, F3 = `0xE20F` alone) to skip the gray-for-inverter registers in between (`0xE206 equalizing-enable` and `0xE207 power-save-level`) which can cause silent timeouts on some firmware.
