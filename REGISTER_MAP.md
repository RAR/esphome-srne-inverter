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

## Block B — inverter (`0x0210`–`0x0224`, 21 regs, polled every cycle)

| Reg | Name | Type | Mult | Unit | Sensor |
|---|---|---|---|---|---|
| 0x0210 | Machine state | u16 | — | enum | `machine_state` (text), `inverter_on` (binary) |
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
| 0x0220 | Heat sink A (DC-DC) | i16 | 0.1 | °C | `heatsink_a_temperature` |
| 0x0221 | Heat sink B (DC-AC) | i16 | 0.1 | °C | `heatsink_b_temperature` |
| 0x0222 | Heat sink C (transformer) | i16 | 0.1 | °C | `heatsink_c_temperature` |
| 0x0224 | PV charge current | u16 | 0.1 | A | `pv_charge_current` |

## Block C — faults (`0x0200`–`0x0207`, 8 regs, polled every cycle)

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
