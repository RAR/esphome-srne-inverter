# Wiring

SRNE inverters expose RS485 (typically on an RJ45 jack labelled "RS485" or "BMS"). You need an RS485 transceiver between the ESP32's UART and the inverter's A/B lines.

## Pinout (RJ45 — verify against your specific inverter manual)

| Pin | Signal |
|---|---|
| 1   | RS485 A |
| 2   | RS485 B |
| 7-8 | GND     |

## Transceiver

Use a 3.3V-compatible RS485 transceiver. Two common options:

- **Auto-direction** (e.g. MAX3485 breakout with auto DE/RE, or MAX13487E) — no GPIO needed for direction control. Leave `flow_control_pin` unset.
- **Manual DE/RE** (e.g. MAX485 / SP3485 modules with DE and RE tied together) — tie DE and RE together and connect to a GPIO. Set `flow_control_pin: GPIOxx` under `srne_modbus:`.

## ESP32 example wiring

| ESP32 | RS485 transceiver |
|---|---|
| GPIO16 (TX) | DI |
| GPIO17 (RX) | RO |
| 3V3         | VCC |
| GND         | GND |
| GPIO4 (optional) | DE/RE (tied together) |

| RS485 transceiver | Inverter RJ45 |
|---|---|
| A | Pin 1 (A) |
| B | Pin 2 (B) |
| GND | Pin 7 or 8 |

## Notes

- 9600 baud, 8N1, no parity.
- One master on the bus; SRNE supports star topology up to 32 slaves.
- The default slave address is 0x01. To change it, write `0xE200` (range 0x01-0xFE) — out of scope for this component.
