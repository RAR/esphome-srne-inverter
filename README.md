# esphome-srne-inverter

ESPHome external component for SRNE hybrid solar inverters via RS485 / Modbus RTU.

Read-only support for battery, PV, grid, inverter, load, heatsink temperatures, machine state, charge state, fault codes, and product info.

## Quickstart

1. Wire your ESP32 to the inverter's RS485 port (see [WIRING.md](WIRING.md)).
2. Copy `secrets.yaml.example` to `secrets.yaml` and fill in your Wi-Fi credentials.
3. Use `esp32-example.yaml` as your starting point (or reference it via `external_components:` from a fresh ESPHome config).
4. `esphome run esp32-example.yaml`.

## Supported entities

### Binary sensors
- `online_status`, `grid_present`, `inverter_on`, `fault`

### Sensors
- Battery: `battery_soc`, `battery_voltage`, `battery_current`
- PV: `pv1_voltage`, `pv1_current`, `pv1_power`, `pv2_voltage`, `pv2_current`, `pv2_power`, `pv_total_power`, `pv_charge_current`, `charge_power`
- Grid: `bus_voltage`, `grid_voltage`, `grid_current`, `grid_frequency`
- Inverter / load: `inverter_voltage`, `inverter_current`, `inverter_frequency`, `load_current`, `load_active_power`, `load_apparent_power`, `load_percent`, `battery_charge_current`
- Heatsinks: `heatsink_a_temperature` (DC-DC), `heatsink_b_temperature` (DC-AC), `heatsink_c_temperature` (transformer)

### Text sensors
- `machine_state`, `charge_state`, `fault_codes`, `software_version`, `hardware_version`, `serial_number`

## Protocol

Implements SRNE's Modbus RTU framing at 9600 8N1 (function `0x03` reads). See [REGISTER_MAP.md](REGISTER_MAP.md) for the subset of registers we decode and `srne-hybrid-solar-inverter-modbus-protocol-v1-7.pdf` for the upstream spec.

Up to 32 registers per read, response timeout 1s, one poll cycle defaults to 10s.

## Out of scope (PRs welcome)

- Writable entities (output priority, charge priority, output voltage, battery type, mains charge current limit, power on/off)
- 3-phase B/C registers (single-phase support only in V1)
- Historical statistics (last-7-days, accumulated AH, working hours)
- Fault history records

## License

Same as the sibling [esphome-eg4-bms](https://github.com/RAR/esphome-eg4-bms) project.
