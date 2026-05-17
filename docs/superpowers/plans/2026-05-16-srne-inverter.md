# SRNE Inverter ESPHome Component Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a read-only ESPHome external component (`srne_modbus` hub + `srne_inverter` device) that reads sensor data from an SRNE hybrid solar inverter over RS485/Modbus RTU.

**Architecture:** Two-component pattern mirroring `esphome-eg4-bms` — a UART Modbus RTU hub with a request queue and CRC16, and a `PollingComponent` device that owns the SRNE register map and decodes responses into ESPHome sensors / binary_sensors / text_sensors.

**Tech Stack:** ESPHome 2026.x external component, C++17, Python codegen for YAML schema. Verification via `esphome config esp32-example.yaml` (validates Python codegen + YAML schema). The reference implementations (eg4_bms, ecoworthy_bms) have no unit-test infrastructure; we follow that convention — `esphome config` is the test loop.

**Spec:** `docs/superpowers/specs/2026-05-16-srne-inverter-design.md`

---

## File Structure

| File | Responsibility |
|---|---|
| `components/srne_modbus/__init__.py` | Python codegen: schema for `srne_modbus:` block, `srne_modbus_device_schema()` helper, `register_srne_modbus_device()` helper |
| `components/srne_modbus/srne_modbus.h` | `SrneModbus` class (UART hub, request queue, parser) + `SrneModbusDevice` abstract base + `crc16_modbus()` |
| `components/srne_modbus/srne_modbus.cpp` | Implementation: setup/loop/send/parse, CRC16 |
| `components/srne_inverter/__init__.py` | Python codegen: schema for `srne_inverter:` block + shared `SRNE_INVERTER_COMPONENT_SCHEMA` re-used by entity platforms |
| `components/srne_inverter/srne_inverter.h` | `SrneInverter` class — setters for every sensor, polling state machine state, decode helpers |
| `components/srne_inverter/srne_inverter.cpp` | Register addresses, `update()` state machine, `on_modbus_data()` dispatch, block decoders, enum→text helpers |
| `components/srne_inverter/sensor.py` | Schema + codegen for all numeric sensors |
| `components/srne_inverter/binary_sensor.py` | Schema + codegen for binary sensors |
| `components/srne_inverter/text_sensor.py` | Schema + codegen for text sensors |
| `esp32-example.yaml` | Full example wiring every supported entity |
| `README.md` | Quickstart, supported entities, install instructions |
| `WIRING.md` | RS485 wiring diagram, flow-control pin notes |
| `REGISTER_MAP.md` | Distilled register table (subset we actually decode) |
| `secrets.yaml.example` | Wi-Fi placeholder |
| `.gitignore` | Build artifacts |

---

## Task 1: Repo scaffolding (.gitignore, secrets example)

**Files:**
- Create: `.gitignore`
- Create: `secrets.yaml.example`

- [ ] **Step 1: Write `.gitignore`**

```
.esphome/
__pycache__/
*.pyc
secrets.yaml
.DS_Store
```

- [ ] **Step 2: Write `secrets.yaml.example`**

```yaml
wifi_ssid: "your-ssid"
wifi_password: "your-password"
```

- [ ] **Step 3: Commit**

```bash
git add .gitignore secrets.yaml.example
git commit -m "chore: add gitignore and secrets template"
```

---

## Task 2: Scaffold `srne_modbus` hub (compiles, does nothing)

The hub should validate via `esphome config` even before it does any real work. Get the Python codegen + minimal C++ skeleton in place first.

**Files:**
- Create: `components/srne_modbus/__init__.py`
- Create: `components/srne_modbus/srne_modbus.h`
- Create: `components/srne_modbus/srne_modbus.cpp`

- [ ] **Step 1: Write `components/srne_modbus/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_FLOW_CONTROL_PIN, CONF_ID

CODEOWNERS = ["@rar"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

CONF_SRNE_MODBUS_ID = "srne_modbus_id"

srne_modbus_ns = cg.esphome_ns.namespace("srne_modbus")
SrneModbus = srne_modbus_ns.class_("SrneModbus", cg.Component, uart.UARTDevice)
SrneModbusDevice = srne_modbus_ns.class_("SrneModbusDevice")

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SrneModbus),
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))


def srne_modbus_device_schema(default_address):
    schema = {
        cv.GenerateID(CONF_SRNE_MODBUS_ID): cv.use_id(SrneModbus),
        cv.Optional("address", default=default_address): cv.hex_uint8_t,
    }
    return cv.Schema(schema)


async def register_srne_modbus_device(var, config):
    parent = await cg.get_variable(config[CONF_SRNE_MODBUS_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_address(config["address"]))
    cg.add(parent.register_device(var))
```

- [ ] **Step 2: Write `components/srne_modbus/srne_modbus.h`**

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include <queue>
#include <vector>

namespace esphome {
namespace srne_modbus {

class SrneModbusDevice;

struct ModbusRequest {
  uint8_t address;
  uint8_t function;
  uint16_t start_register;
  uint16_t num_registers;
  std::vector<uint8_t> payload;  // only used for 0x06 / 0x10
};

class SrneModbus : public uart::UARTDevice, public Component {
 public:
  SrneModbus() = default;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_flow_control_pin(GPIOPin *flow_control_pin) { this->flow_control_pin_ = flow_control_pin; }
  void register_device(SrneModbusDevice *device) { this->devices_.push_back(device); }

  void send(uint8_t address, uint8_t function, uint16_t start_register, uint16_t num_registers);

 protected:
  bool parse_modbus_byte_(uint8_t byte);
  void send_next_request_();

  GPIOPin *flow_control_pin_{nullptr};
  std::vector<uint8_t> rx_buffer_;
  uint32_t last_modbus_byte_{0};
  uint32_t last_send_{0};
  std::vector<SrneModbusDevice *> devices_;
  std::queue<ModbusRequest> request_queue_;
  bool waiting_for_response_{false};
};

uint16_t crc16_modbus(const uint8_t *data, uint16_t len);

class SrneModbusDevice {
 public:
  void set_parent(SrneModbus *parent) { parent_ = parent; }
  void set_address(uint8_t address) { address_ = address; }
  uint8_t get_address() const { return address_; }

  virtual void on_modbus_data(const std::vector<uint8_t> &data) = 0;

  void send(uint8_t function, uint16_t start_register, uint16_t num_registers) {
    this->parent_->send(this->address_, function, start_register, num_registers);
  }

 protected:
  friend SrneModbus;
  SrneModbus *parent_;
  uint8_t address_;
};

}  // namespace srne_modbus
}  // namespace esphome
```

- [ ] **Step 3: Write `components/srne_modbus/srne_modbus.cpp` (stub body)**

```cpp
#include "srne_modbus.h"
#include "esphome/core/log.h"

namespace esphome {
namespace srne_modbus {

static const char *const TAG = "srne_modbus";

void SrneModbus::setup() {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
  }
}

void SrneModbus::loop() {}

void SrneModbus::dump_config() {
  ESP_LOGCONFIG(TAG, "SRNE Modbus:");
  ESP_LOGCONFIG(TAG, "  Flow control pin: %s", YESNO(this->flow_control_pin_ != nullptr));
}

float SrneModbus::get_setup_priority() const { return setup_priority::DATA; }

uint16_t crc16_modbus(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void SrneModbus::send(uint8_t address, uint8_t function, uint16_t start_register, uint16_t num_registers) {
  ModbusRequest request{address, function, start_register, num_registers, {}};
  this->request_queue_.push(request);
}

void SrneModbus::send_next_request_() {}

bool SrneModbus::parse_modbus_byte_(uint8_t /*byte*/) { return true; }

}  // namespace srne_modbus
}  // namespace esphome
```

- [ ] **Step 4: Verify the component imports cleanly**

Write a temporary minimal YAML at `/tmp/srne-validate-1.yaml`:

```yaml
esphome:
  name: srne-test
esp32:
  board: wemos_d1_mini32
external_components:
  - source:
      type: local
      path: components
    components: [srne_modbus]
logger:
uart:
  id: uart_0
  baud_rate: 9600
  tx_pin: GPIO16
  rx_pin: GPIO17
srne_modbus:
  id: modbus0
  uart_id: uart_0
```

Run: `cd /home/rar/esphome-srne-inverter && esphome config /tmp/srne-validate-1.yaml 2>&1 | tail -30`
Expected: prints the merged config without errors, ends with the `srne_modbus:` block resolved (no traceback).

- [ ] **Step 5: Commit**

```bash
git add components/srne_modbus/
git commit -m "feat(srne_modbus): scaffold UART hub component"
```

---

## Task 3: Implement Modbus RTU framing in `srne_modbus.cpp`

Replace the stubs from Task 2 with the real send/parse loop. Same algorithm as `eg4_modbus.cpp` lines 22–191.

**Files:**
- Modify: `components/srne_modbus/srne_modbus.cpp`

- [ ] **Step 1: Replace `loop()`, `send_next_request_()`, and `parse_modbus_byte_()` with real implementations**

Replace the three stub functions in `srne_modbus.cpp` with these. Keep `setup`, `dump_config`, `get_setup_priority`, `crc16_modbus`, and `send` (queue push) from Task 2.

Add constants at the top of the namespace block (after `static const char *const TAG`):

```cpp
static const uint8_t MODBUS_READ_HOLDING_REGISTERS = 0x03;
static const uint8_t MODBUS_WRITE_SINGLE_REGISTER = 0x06;
static const uint8_t MODBUS_WRITE_MULTIPLE_REGISTERS = 0x10;

static const uint16_t SRNE_MODBUS_RESPONSE_TIMEOUT = 1000;
static const uint16_t SRNE_MODBUS_MIN_MSG_LEN = 5;
```

Replace `loop()`:

```cpp
void SrneModbus::loop() {
  const uint32_t now = millis();

  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    if (this->parse_modbus_byte_(byte)) {
      this->last_modbus_byte_ = now;
    } else {
      this->rx_buffer_.clear();
    }
  }

  if (this->waiting_for_response_ && !this->rx_buffer_.empty() &&
      (now - this->last_modbus_byte_ > SRNE_MODBUS_RESPONSE_TIMEOUT)) {
    ESP_LOGW(TAG, "Modbus response timeout (partial frame)");
    this->rx_buffer_.clear();
    this->waiting_for_response_ = false;
  }

  if (this->waiting_for_response_ && this->rx_buffer_.empty() &&
      (now - this->last_send_ > SRNE_MODBUS_RESPONSE_TIMEOUT)) {
    ESP_LOGW(TAG, "No Modbus response received");
    this->waiting_for_response_ = false;
  }

  if (!this->waiting_for_response_) {
    this->send_next_request_();
  }
}
```

Replace `send_next_request_()`:

```cpp
void SrneModbus::send_next_request_() {
  if (this->request_queue_.empty() || this->waiting_for_response_) {
    return;
  }

  ModbusRequest request = this->request_queue_.front();
  this->request_queue_.pop();

  uint8_t frame[8];
  frame[0] = request.address;
  frame[1] = request.function;
  frame[2] = request.start_register >> 8;
  frame[3] = request.start_register & 0xFF;
  frame[4] = request.num_registers >> 8;
  frame[5] = request.num_registers & 0xFF;

  uint16_t crc = crc16_modbus(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;

  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->digital_write(true);
  }

  this->write_array(frame, 8);
  this->flush();

  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->digital_write(false);
  }

  ESP_LOGV(TAG, "Sent: %s", format_hex_pretty(frame, 8).c_str());
  this->last_send_ = millis();
  this->waiting_for_response_ = true;
}
```

Replace `parse_modbus_byte_()`:

```cpp
bool SrneModbus::parse_modbus_byte_(uint8_t byte) {
  if (this->rx_buffer_.empty()) {
    this->rx_buffer_.push_back(byte);
    return true;
  }

  this->rx_buffer_.push_back(byte);
  const uint8_t *raw = this->rx_buffer_.data();
  const size_t len = this->rx_buffer_.size();

  if (len < SRNE_MODBUS_MIN_MSG_LEN) {
    return true;
  }

  uint8_t function = raw[1];
  size_t expected_len = 0;

  if (function == MODBUS_READ_HOLDING_REGISTERS) {
    uint8_t byte_count = raw[2];
    expected_len = 3 + byte_count + 2;  // addr + func + count + data + crc
  } else if (function == MODBUS_WRITE_SINGLE_REGISTER ||
             function == MODBUS_WRITE_MULTIPLE_REGISTERS) {
    expected_len = 8;  // addr + func + reg_hi + reg_lo + val_hi + val_lo + crc x2
  } else if ((function & 0x80) != 0) {
    expected_len = 5;  // addr + (func|0x80) + error + crc x2
  }

  if (expected_len == 0 || len < expected_len) {
    return true;
  }

  uint16_t crc_calc = crc16_modbus(raw, expected_len - 2);
  uint16_t crc_recv = raw[expected_len - 2] | (raw[expected_len - 1] << 8);

  if (crc_calc != crc_recv) {
    ESP_LOGW(TAG, "Modbus CRC check failed! Calculated: 0x%04X, Received: 0x%04X", crc_calc, crc_recv);
    this->rx_buffer_.clear();
    this->waiting_for_response_ = false;
    return false;
  }

  ESP_LOGV(TAG, "Received: %s", format_hex_pretty(raw, expected_len).c_str());

  for (auto *device : this->devices_) {
    device->on_modbus_data(this->rx_buffer_);
  }

  this->rx_buffer_.clear();
  this->waiting_for_response_ = false;
  return true;
}
```

Also add `#include "esphome/core/helpers.h"` near the top of the file (needed by `format_hex_pretty`).

- [ ] **Step 2: Verify config still validates**

Run: `cd /home/rar/esphome-srne-inverter && esphome config /tmp/srne-validate-1.yaml 2>&1 | tail -10`
Expected: validates cleanly. (We can't compile yet without a device registered — that comes in Task 4.)

- [ ] **Step 3: Commit**

```bash
git add components/srne_modbus/srne_modbus.cpp
git commit -m "feat(srne_modbus): implement Modbus RTU framing with CRC16"
```

---

## Task 4: Scaffold `srne_inverter` device (compiles, polls nothing)

Standing skeleton so `esphome config` validates the full chain `srne_modbus` → `srne_inverter`.

**Files:**
- Create: `components/srne_inverter/__init__.py`
- Create: `components/srne_inverter/srne_inverter.h`
- Create: `components/srne_inverter/srne_inverter.cpp`

- [ ] **Step 1: Write `components/srne_inverter/__init__.py`**

```python
import esphome.codegen as cg
from esphome.components import srne_modbus
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["srne_modbus", "binary_sensor", "sensor", "text_sensor"]
CODEOWNERS = ["@rar"]
MULTI_CONF = True

CONF_SRNE_INVERTER_ID = "srne_inverter_id"

DEFAULT_ADDRESS = 0x01

srne_inverter_ns = cg.esphome_ns.namespace("srne_inverter")
SrneInverter = srne_inverter_ns.class_("SrneInverter", cg.PollingComponent, srne_modbus.SrneModbusDevice)

SRNE_INVERTER_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SRNE_INVERTER_ID): cv.use_id(SrneInverter),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SrneInverter),
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(srne_modbus.srne_modbus_device_schema(DEFAULT_ADDRESS))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await srne_modbus.register_srne_modbus_device(var, config)
```

- [ ] **Step 2: Write `components/srne_inverter/srne_inverter.h`**

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/srne_modbus/srne_modbus.h"

namespace esphome {
namespace srne_inverter {

class SrneInverter : public PollingComponent, public srne_modbus::SrneModbusDevice {
 public:
  void setup() override {}
  void loop() override {}
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void on_modbus_data(const std::vector<uint8_t> &data) override;
};

}  // namespace srne_inverter
}  // namespace esphome
```

- [ ] **Step 3: Write `components/srne_inverter/srne_inverter.cpp` (stub bodies)**

```cpp
#include "srne_inverter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace srne_inverter {

static const char *const TAG = "srne_inverter";

void SrneInverter::dump_config() {
  ESP_LOGCONFIG(TAG, "SRNE Inverter:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
}

float SrneInverter::get_setup_priority() const { return setup_priority::DATA; }

void SrneInverter::update() {}

void SrneInverter::on_modbus_data(const std::vector<uint8_t> & /*data*/) {}

}  // namespace srne_inverter
}  // namespace esphome
```

- [ ] **Step 4: Verify with extended config**

Write `/tmp/srne-validate-2.yaml`:

```yaml
esphome:
  name: srne-test
esp32:
  board: wemos_d1_mini32
external_components:
  - source:
      type: local
      path: components
    components: [srne_modbus, srne_inverter]
logger:
uart:
  id: uart_0
  baud_rate: 9600
  tx_pin: GPIO16
  rx_pin: GPIO17
srne_modbus:
  id: modbus0
  uart_id: uart_0
srne_inverter:
  id: inv0
  srne_modbus_id: modbus0
  address: 0x01
  update_interval: 10s
```

Run: `cd /home/rar/esphome-srne-inverter && esphome config /tmp/srne-validate-2.yaml 2>&1 | tail -20`
Expected: validates, prints `srne_inverter:` block.

- [ ] **Step 5: Commit**

```bash
git add components/srne_inverter/
git commit -m "feat(srne_inverter): scaffold polling device component"
```

---

## Task 5: Add sensor.py and block A entities (battery + PV)

Wire up the first set of sensors and implement the block A poll + decoder. After this task, on real hardware, battery + PV sensors will be live.

**Files:**
- Create: `components/srne_inverter/sensor.py`
- Modify: `components/srne_inverter/srne_inverter.h`
- Modify: `components/srne_inverter/srne_inverter.cpp`

- [ ] **Step 1: Write `components/srne_inverter/sensor.py`**

```python
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_CURRENT,
    CONF_FREQUENCY,
    CONF_POWER,
    CONF_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID

DEPENDENCIES = ["srne_inverter"]

# Block A (controller / PV)
CONF_BATTERY_SOC = "battery_soc"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_CURRENT = "battery_current"
CONF_PV1_VOLTAGE = "pv1_voltage"
CONF_PV1_CURRENT = "pv1_current"
CONF_PV1_POWER = "pv1_power"
CONF_PV2_VOLTAGE = "pv2_voltage"
CONF_PV2_CURRENT = "pv2_current"
CONF_PV2_POWER = "pv2_power"
CONF_PV_TOTAL_POWER = "pv_total_power"
CONF_CHARGE_POWER = "charge_power"

UNIT_VOLT_AMPS = "VA"

VOLTAGE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_VOLT,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_VOLTAGE,
    state_class=STATE_CLASS_MEASUREMENT,
)
CURRENT_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_AMPERE,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_CURRENT,
    state_class=STATE_CLASS_MEASUREMENT,
)
POWER_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_WATT,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_POWER,
    state_class=STATE_CLASS_MEASUREMENT,
)
PERCENT_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_PERCENT,
    accuracy_decimals=0,
    state_class=STATE_CLASS_MEASUREMENT,
)

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_BATTERY_SOC): PERCENT_SCHEMA,
        cv.Optional(CONF_BATTERY_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_BATTERY_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_PV1_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_PV1_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_PV1_POWER): POWER_SCHEMA,
        cv.Optional(CONF_PV2_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_PV2_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_PV2_POWER): POWER_SCHEMA,
        cv.Optional(CONF_PV_TOTAL_POWER): POWER_SCHEMA,
        cv.Optional(CONF_CHARGE_POWER): POWER_SCHEMA,
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    mapping = {
        CONF_BATTERY_SOC: hub.set_battery_soc_sensor,
        CONF_BATTERY_VOLTAGE: hub.set_battery_voltage_sensor,
        CONF_BATTERY_CURRENT: hub.set_battery_current_sensor,
        CONF_PV1_VOLTAGE: hub.set_pv1_voltage_sensor,
        CONF_PV1_CURRENT: hub.set_pv1_current_sensor,
        CONF_PV1_POWER: hub.set_pv1_power_sensor,
        CONF_PV2_VOLTAGE: hub.set_pv2_voltage_sensor,
        CONF_PV2_CURRENT: hub.set_pv2_current_sensor,
        CONF_PV2_POWER: hub.set_pv2_power_sensor,
        CONF_PV_TOTAL_POWER: hub.set_pv_total_power_sensor,
        CONF_CHARGE_POWER: hub.set_charge_power_sensor,
    }
    for key, setter in mapping.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(setter(sens))
```

(Note: this file imports `CONF_CURRENT`, `CONF_FREQUENCY`, `CONF_POWER`, `CONF_VOLTAGE`, several UNIT_* and DEVICE_CLASS_* constants that will be used by later additions; harmless if unused for now but Python will not flag them.)

- [ ] **Step 2: Replace `srne_inverter.h` with the full setter-bearing version**

This replaces Task 4 Step 2's stub.

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/srne_modbus/srne_modbus.h"

namespace esphome {
namespace srne_inverter {

class SrneInverter : public PollingComponent, public srne_modbus::SrneModbusDevice {
 public:
  void setup() override {}
  void loop() override {}
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void on_modbus_data(const std::vector<uint8_t> &data) override;

  // Block A sensors
  void set_battery_soc_sensor(sensor::Sensor *s) { battery_soc_sensor_ = s; }
  void set_battery_voltage_sensor(sensor::Sensor *s) { battery_voltage_sensor_ = s; }
  void set_battery_current_sensor(sensor::Sensor *s) { battery_current_sensor_ = s; }
  void set_pv1_voltage_sensor(sensor::Sensor *s) { pv1_voltage_sensor_ = s; }
  void set_pv1_current_sensor(sensor::Sensor *s) { pv1_current_sensor_ = s; }
  void set_pv1_power_sensor(sensor::Sensor *s) { pv1_power_sensor_ = s; }
  void set_pv2_voltage_sensor(sensor::Sensor *s) { pv2_voltage_sensor_ = s; }
  void set_pv2_current_sensor(sensor::Sensor *s) { pv2_current_sensor_ = s; }
  void set_pv2_power_sensor(sensor::Sensor *s) { pv2_power_sensor_ = s; }
  void set_pv_total_power_sensor(sensor::Sensor *s) { pv_total_power_sensor_ = s; }
  void set_charge_power_sensor(sensor::Sensor *s) { charge_power_sensor_ = s; }

 protected:
  // Block A storage
  sensor::Sensor *battery_soc_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_current_sensor_{nullptr};
  sensor::Sensor *pv1_voltage_sensor_{nullptr};
  sensor::Sensor *pv1_current_sensor_{nullptr};
  sensor::Sensor *pv1_power_sensor_{nullptr};
  sensor::Sensor *pv2_voltage_sensor_{nullptr};
  sensor::Sensor *pv2_current_sensor_{nullptr};
  sensor::Sensor *pv2_power_sensor_{nullptr};
  sensor::Sensor *pv_total_power_sensor_{nullptr};
  sensor::Sensor *charge_power_sensor_{nullptr};

  uint8_t request_step_{0};
  uint8_t last_request_step_{0xFF};

  void publish_state_(sensor::Sensor *s, float value);
  void decode_block_a_(const uint8_t *payload, size_t byte_count);
};

}  // namespace srne_inverter
}  // namespace esphome
```

- [ ] **Step 3: Replace `srne_inverter.cpp` with the polling + block A decoder**

```cpp
#include "srne_inverter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace srne_inverter {

static const char *const TAG = "srne_inverter";

static const uint8_t FUNCTION_READ_HOLDING = 0x03;

// Block A: controller / PV — 0x0100..0x0111 (18 regs, 36 bytes)
static const uint16_t REG_BLOCK_A_START = 0x0100;
static const uint16_t REG_BLOCK_A_COUNT = 0x12;
static const uint8_t BLOCK_A_BYTE_COUNT = 0x24;

void SrneInverter::dump_config() {
  ESP_LOGCONFIG(TAG, "SRNE Inverter:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
  LOG_SENSOR("  ", "Battery SOC", this->battery_soc_sensor_);
  LOG_SENSOR("  ", "Battery V", this->battery_voltage_sensor_);
  LOG_SENSOR("  ", "Battery A", this->battery_current_sensor_);
  LOG_SENSOR("  ", "PV1 V", this->pv1_voltage_sensor_);
  LOG_SENSOR("  ", "PV1 A", this->pv1_current_sensor_);
  LOG_SENSOR("  ", "PV1 W", this->pv1_power_sensor_);
  LOG_SENSOR("  ", "PV2 V", this->pv2_voltage_sensor_);
  LOG_SENSOR("  ", "PV2 A", this->pv2_current_sensor_);
  LOG_SENSOR("  ", "PV2 W", this->pv2_power_sensor_);
  LOG_SENSOR("  ", "PV Total W", this->pv_total_power_sensor_);
  LOG_SENSOR("  ", "Charge W", this->charge_power_sensor_);
}

float SrneInverter::get_setup_priority() const { return setup_priority::DATA; }

void SrneInverter::update() {
  // For now, only block A. Block B/C/D/E added in later tasks.
  this->last_request_step_ = 0;
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_A_START, REG_BLOCK_A_COUNT);
}

void SrneInverter::on_modbus_data(const std::vector<uint8_t> &data) {
  if (data.size() < 5) return;

  uint8_t address = data[0];
  uint8_t function = data[1];

  if (address != this->address_) return;

  if ((function & 0x80) != 0) {
    ESP_LOGW(TAG, "Modbus error response: 0x%02X", data[2]);
    return;
  }

  if (function != FUNCTION_READ_HOLDING) {
    return;
  }

  uint8_t byte_count = data[2];
  if (data.size() < (size_t)(3 + byte_count + 2)) {
    ESP_LOGW(TAG, "Truncated response");
    return;
  }

  const uint8_t *payload = data.data() + 3;

  // Dispatch by (request_step, byte_count). For now only block A.
  if (this->last_request_step_ == 0 && byte_count == BLOCK_A_BYTE_COUNT) {
    this->decode_block_a_(payload, byte_count);
  } else {
    ESP_LOGW(TAG, "Unexpected response: step=%u byte_count=%u",
             this->last_request_step_, byte_count);
  }
}

static inline uint16_t get_u16(const uint8_t *p, size_t i) {
  return (uint16_t(p[i]) << 8) | uint16_t(p[i + 1]);
}
static inline int16_t get_i16(const uint8_t *p, size_t i) {
  return (int16_t) get_u16(p, i);
}

void SrneInverter::decode_block_a_(const uint8_t *p, size_t /*byte_count*/) {
  // Layout (each register is 2 bytes; offset = (reg - 0x0100) * 2)
  // 0x0100 SOC | 0x0101 V x0.1 | 0x0102 I x0.1 signed | ...
  this->publish_state_(this->battery_soc_sensor_, (float) get_u16(p, 0));
  this->publish_state_(this->battery_voltage_sensor_, get_u16(p, 2) * 0.1f);
  this->publish_state_(this->battery_current_sensor_, get_i16(p, 4) * 0.1f);
  // 0x0103 device temp (skip), 0x0104-0x0106 DC load (gray, skip)
  // 0x0107 PV1 V, 0x0108 PV1 I, 0x0109 PV1 W
  this->publish_state_(this->pv1_voltage_sensor_, get_u16(p, 14) * 0.1f);
  this->publish_state_(this->pv1_current_sensor_, get_u16(p, 16) * 0.1f);
  uint16_t pv1_w = get_u16(p, 18);
  this->publish_state_(this->pv1_power_sensor_, (float) pv1_w);
  // 0x010A DC load on/off (skip), 0x010B charge_state (handled in text_sensor task)
  // 0x010C-0x010D fault msg (skip), 0x010E charge_power
  this->publish_state_(this->charge_power_sensor_, (float) get_u16(p, 28));
  // 0x010F PV2 V, 0x0110 PV2 I, 0x0111 PV2 W
  this->publish_state_(this->pv2_voltage_sensor_, get_u16(p, 30) * 0.1f);
  this->publish_state_(this->pv2_current_sensor_, get_u16(p, 32) * 0.1f);
  uint16_t pv2_w = get_u16(p, 34);
  this->publish_state_(this->pv2_power_sensor_, (float) pv2_w);
  this->publish_state_(this->pv_total_power_sensor_, (float) (pv1_w + pv2_w));
}

void SrneInverter::publish_state_(sensor::Sensor *s, float value) {
  if (s != nullptr && !std::isnan(value)) {
    s->publish_state(value);
  }
}

}  // namespace srne_inverter
}  // namespace esphome
```

- [ ] **Step 4: Verify with sensor-enabled YAML**

Write `/tmp/srne-validate-3.yaml`:

```yaml
esphome:
  name: srne-test
esp32:
  board: wemos_d1_mini32
external_components:
  - source:
      type: local
      path: components
    components: [srne_modbus, srne_inverter]
logger:
uart:
  id: uart_0
  baud_rate: 9600
  tx_pin: GPIO16
  rx_pin: GPIO17
srne_modbus:
  id: modbus0
  uart_id: uart_0
srne_inverter:
  id: inv0
  srne_modbus_id: modbus0
  address: 0x01
sensor:
  - platform: srne_inverter
    srne_inverter_id: inv0
    battery_soc: { name: "Battery SOC" }
    battery_voltage: { name: "Battery V" }
    battery_current: { name: "Battery A" }
    pv1_voltage: { name: "PV1 V" }
    pv1_current: { name: "PV1 A" }
    pv1_power: { name: "PV1 W" }
    pv2_voltage: { name: "PV2 V" }
    pv2_current: { name: "PV2 A" }
    pv2_power: { name: "PV2 W" }
    pv_total_power: { name: "PV Total W" }
    charge_power: { name: "Charge W" }
```

Run: `cd /home/rar/esphome-srne-inverter && esphome config /tmp/srne-validate-3.yaml 2>&1 | tail -20`
Expected: validates, shows all 11 sensors.

- [ ] **Step 5: Commit**

```bash
git add components/srne_inverter/
git commit -m "feat(srne_inverter): add battery and PV sensors (block A)"
```

---

## Task 6: Add block B sensors (grid, inverter, load, heatsinks)

Extend `sensor.py`, `srne_inverter.h`, and `srne_inverter.cpp` with the inverter-data block.

**Files:**
- Modify: `components/srne_inverter/sensor.py`
- Modify: `components/srne_inverter/srne_inverter.h`
- Modify: `components/srne_inverter/srne_inverter.cpp`

- [ ] **Step 1: Extend `sensor.py` — add block B config keys and schemas**

Add to the `# Block A (controller / PV)` section block in `sensor.py`, after `CONF_CHARGE_POWER`:

```python

# Block B (inverter)
CONF_BUS_VOLTAGE = "bus_voltage"
CONF_GRID_VOLTAGE = "grid_voltage"
CONF_GRID_CURRENT = "grid_current"
CONF_GRID_FREQUENCY = "grid_frequency"
CONF_INVERTER_VOLTAGE = "inverter_voltage"
CONF_INVERTER_CURRENT = "inverter_current"
CONF_INVERTER_FREQUENCY = "inverter_frequency"
CONF_LOAD_CURRENT = "load_current"
CONF_LOAD_ACTIVE_POWER = "load_active_power"
CONF_LOAD_APPARENT_POWER = "load_apparent_power"
CONF_LOAD_PERCENT = "load_percent"
CONF_BATTERY_CHARGE_CURRENT = "battery_charge_current"
CONF_PV_CHARGE_CURRENT = "pv_charge_current"
CONF_HEATSINK_A_TEMPERATURE = "heatsink_a_temperature"
CONF_HEATSINK_B_TEMPERATURE = "heatsink_b_temperature"
CONF_HEATSINK_C_TEMPERATURE = "heatsink_c_temperature"
```

Then add these schemas above the `CONFIG_SCHEMA =` assignment (next to the existing VOLTAGE_SCHEMA / CURRENT_SCHEMA etc.):

```python
FREQUENCY_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_HERTZ,
    accuracy_decimals=2,
    device_class=DEVICE_CLASS_FREQUENCY,
    state_class=STATE_CLASS_MEASUREMENT,
)
APPARENT_POWER_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_VOLT_AMPS,
    accuracy_decimals=0,
    state_class=STATE_CLASS_MEASUREMENT,
)
TEMPERATURE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)
```

Then extend the `CONFIG_SCHEMA` dict (inside the existing `.extend({...})`) by adding these entries before the closing `}`:

```python
        cv.Optional(CONF_BUS_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_GRID_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_GRID_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_GRID_FREQUENCY): FREQUENCY_SCHEMA,
        cv.Optional(CONF_INVERTER_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_INVERTER_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_INVERTER_FREQUENCY): FREQUENCY_SCHEMA,
        cv.Optional(CONF_LOAD_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_LOAD_ACTIVE_POWER): POWER_SCHEMA,
        cv.Optional(CONF_LOAD_APPARENT_POWER): APPARENT_POWER_SCHEMA,
        cv.Optional(CONF_LOAD_PERCENT): PERCENT_SCHEMA,
        cv.Optional(CONF_BATTERY_CHARGE_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_PV_CHARGE_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_HEATSINK_A_TEMPERATURE): TEMPERATURE_SCHEMA,
        cv.Optional(CONF_HEATSINK_B_TEMPERATURE): TEMPERATURE_SCHEMA,
        cv.Optional(CONF_HEATSINK_C_TEMPERATURE): TEMPERATURE_SCHEMA,
```

Then extend the `mapping` dict in `to_code` (after `CONF_CHARGE_POWER: hub.set_charge_power_sensor,`):

```python
        CONF_BUS_VOLTAGE: hub.set_bus_voltage_sensor,
        CONF_GRID_VOLTAGE: hub.set_grid_voltage_sensor,
        CONF_GRID_CURRENT: hub.set_grid_current_sensor,
        CONF_GRID_FREQUENCY: hub.set_grid_frequency_sensor,
        CONF_INVERTER_VOLTAGE: hub.set_inverter_voltage_sensor,
        CONF_INVERTER_CURRENT: hub.set_inverter_current_sensor,
        CONF_INVERTER_FREQUENCY: hub.set_inverter_frequency_sensor,
        CONF_LOAD_CURRENT: hub.set_load_current_sensor,
        CONF_LOAD_ACTIVE_POWER: hub.set_load_active_power_sensor,
        CONF_LOAD_APPARENT_POWER: hub.set_load_apparent_power_sensor,
        CONF_LOAD_PERCENT: hub.set_load_percent_sensor,
        CONF_BATTERY_CHARGE_CURRENT: hub.set_battery_charge_current_sensor,
        CONF_PV_CHARGE_CURRENT: hub.set_pv_charge_current_sensor,
        CONF_HEATSINK_A_TEMPERATURE: hub.set_heatsink_a_temperature_sensor,
        CONF_HEATSINK_B_TEMPERATURE: hub.set_heatsink_b_temperature_sensor,
        CONF_HEATSINK_C_TEMPERATURE: hub.set_heatsink_c_temperature_sensor,
```

- [ ] **Step 2: Extend `srne_inverter.h` — add setters and storage for block B**

After the block A storage declarations (before the `uint8_t request_step_` line), add:

```cpp
  // Block B sensors
  sensor::Sensor *bus_voltage_sensor_{nullptr};
  sensor::Sensor *grid_voltage_sensor_{nullptr};
  sensor::Sensor *grid_current_sensor_{nullptr};
  sensor::Sensor *grid_frequency_sensor_{nullptr};
  sensor::Sensor *inverter_voltage_sensor_{nullptr};
  sensor::Sensor *inverter_current_sensor_{nullptr};
  sensor::Sensor *inverter_frequency_sensor_{nullptr};
  sensor::Sensor *load_current_sensor_{nullptr};
  sensor::Sensor *load_active_power_sensor_{nullptr};
  sensor::Sensor *load_apparent_power_sensor_{nullptr};
  sensor::Sensor *load_percent_sensor_{nullptr};
  sensor::Sensor *battery_charge_current_sensor_{nullptr};
  sensor::Sensor *pv_charge_current_sensor_{nullptr};
  sensor::Sensor *heatsink_a_temperature_sensor_{nullptr};
  sensor::Sensor *heatsink_b_temperature_sensor_{nullptr};
  sensor::Sensor *heatsink_c_temperature_sensor_{nullptr};
```

And add the decode helper to the protected declarations (after `decode_block_a_`):

```cpp
  void decode_block_b_(const uint8_t *payload, size_t byte_count);
```

In the public section after `set_charge_power_sensor`, add:

```cpp
  void set_bus_voltage_sensor(sensor::Sensor *s) { bus_voltage_sensor_ = s; }
  void set_grid_voltage_sensor(sensor::Sensor *s) { grid_voltage_sensor_ = s; }
  void set_grid_current_sensor(sensor::Sensor *s) { grid_current_sensor_ = s; }
  void set_grid_frequency_sensor(sensor::Sensor *s) { grid_frequency_sensor_ = s; }
  void set_inverter_voltage_sensor(sensor::Sensor *s) { inverter_voltage_sensor_ = s; }
  void set_inverter_current_sensor(sensor::Sensor *s) { inverter_current_sensor_ = s; }
  void set_inverter_frequency_sensor(sensor::Sensor *s) { inverter_frequency_sensor_ = s; }
  void set_load_current_sensor(sensor::Sensor *s) { load_current_sensor_ = s; }
  void set_load_active_power_sensor(sensor::Sensor *s) { load_active_power_sensor_ = s; }
  void set_load_apparent_power_sensor(sensor::Sensor *s) { load_apparent_power_sensor_ = s; }
  void set_load_percent_sensor(sensor::Sensor *s) { load_percent_sensor_ = s; }
  void set_battery_charge_current_sensor(sensor::Sensor *s) { battery_charge_current_sensor_ = s; }
  void set_pv_charge_current_sensor(sensor::Sensor *s) { pv_charge_current_sensor_ = s; }
  void set_heatsink_a_temperature_sensor(sensor::Sensor *s) { heatsink_a_temperature_sensor_ = s; }
  void set_heatsink_b_temperature_sensor(sensor::Sensor *s) { heatsink_b_temperature_sensor_ = s; }
  void set_heatsink_c_temperature_sensor(sensor::Sensor *s) { heatsink_c_temperature_sensor_ = s; }
```

- [ ] **Step 3: Update `srne_inverter.cpp` — add block B constants, polling, decoder**

Add after the block A constants:

```cpp
// Block B: inverter — 0x0210..0x0224 (21 regs, 42 bytes)
static const uint16_t REG_BLOCK_B_START = 0x0210;
static const uint16_t REG_BLOCK_B_COUNT = 0x15;
static const uint8_t BLOCK_B_BYTE_COUNT = 0x2A;
```

Replace `update()` with a 2-step rotation:

```cpp
void SrneInverter::update() {
  switch (this->request_step_) {
    case 0:
      this->last_request_step_ = 0;
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_A_START, REG_BLOCK_A_COUNT);
      break;
    case 1:
      this->last_request_step_ = 1;
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B_START, REG_BLOCK_B_COUNT);
      break;
  }
  this->request_step_ = (this->request_step_ + 1) % 2;
}
```

Wait — `last_request_step_` must be set on the *last* request we issued so dispatch matches it. Since both blocks are queued the same poll cycle, we need a different scheme. Use a per-cycle FIFO that we read at dispatch time. Replace `update()` with:

```cpp
void SrneInverter::update() {
  this->expected_steps_.push(0);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_A_START, REG_BLOCK_A_COUNT);
  this->expected_steps_.push(1);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B_START, REG_BLOCK_B_COUNT);
}
```

Update `on_modbus_data` to pop from the queue:

```cpp
void SrneInverter::on_modbus_data(const std::vector<uint8_t> &data) {
  if (data.size() < 5) return;

  uint8_t address = data[0];
  uint8_t function = data[1];

  if (address != this->address_) return;

  if ((function & 0x80) != 0) {
    ESP_LOGW(TAG, "Modbus error response: 0x%02X", data[2]);
    if (!this->expected_steps_.empty()) this->expected_steps_.pop();
    return;
  }

  if (function != FUNCTION_READ_HOLDING) return;

  uint8_t byte_count = data[2];
  if (data.size() < (size_t)(3 + byte_count + 2)) return;

  if (this->expected_steps_.empty()) {
    ESP_LOGW(TAG, "Unexpected response (no queued step)");
    return;
  }

  uint8_t step = this->expected_steps_.front();
  this->expected_steps_.pop();

  const uint8_t *payload = data.data() + 3;

  switch (step) {
    case 0:
      if (byte_count == BLOCK_A_BYTE_COUNT) this->decode_block_a_(payload, byte_count);
      break;
    case 1:
      if (byte_count == BLOCK_B_BYTE_COUNT) this->decode_block_b_(payload, byte_count);
      break;
  }
}
```

In `srne_inverter.h`, replace the `request_step_` / `last_request_step_` declarations with:

```cpp
  std::queue<uint8_t> expected_steps_;
```

And add `#include <queue>` at the top of `srne_inverter.h`.

Also remove the now-unused `request_step_` and `last_request_step_` from the protected section.

Add `decode_block_b_`:

```cpp
void SrneInverter::decode_block_b_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_B_BYTE_COUNT) return;
  // Offsets from 0x0210:
  // 0x0210 state (text), 0x0211 password mark (skip)
  // 0x0212 bus V, 0x0213 grid V, 0x0214 grid I, 0x0215 grid Hz x0.01
  this->publish_state_(this->bus_voltage_sensor_, get_u16(p, 4) * 0.1f);
  this->publish_state_(this->grid_voltage_sensor_, get_u16(p, 6) * 0.1f);
  this->publish_state_(this->grid_current_sensor_, get_u16(p, 8) * 0.1f);
  this->publish_state_(this->grid_frequency_sensor_, get_u16(p, 10) * 0.01f);
  // 0x0216 inverter V, 0x0217 inverter I, 0x0218 inverter Hz x0.01
  this->publish_state_(this->inverter_voltage_sensor_, get_u16(p, 12) * 0.1f);
  this->publish_state_(this->inverter_current_sensor_, get_u16(p, 14) * 0.1f);
  this->publish_state_(this->inverter_frequency_sensor_, get_u16(p, 16) * 0.01f);
  // 0x0219 load I, 0x021A load PF (gray skip), 0x021B load W, 0x021C load VA, 0x021D DC component (gray skip)
  this->publish_state_(this->load_current_sensor_, get_u16(p, 18) * 0.1f);
  this->publish_state_(this->load_active_power_sensor_, (float) get_u16(p, 22));
  this->publish_state_(this->load_apparent_power_sensor_, (float) get_u16(p, 24));
  // 0x021E battery charge I, 0x021F load %
  this->publish_state_(this->battery_charge_current_sensor_, get_u16(p, 28) * 0.1f);
  this->publish_state_(this->load_percent_sensor_, (float) get_u16(p, 30));
  // 0x0220-0x0222 heatsinks A/B/C (signed, x0.1), 0x0223 D (gray skip)
  this->publish_state_(this->heatsink_a_temperature_sensor_, get_i16(p, 32) * 0.1f);
  this->publish_state_(this->heatsink_b_temperature_sensor_, get_i16(p, 34) * 0.1f);
  this->publish_state_(this->heatsink_c_temperature_sensor_, get_i16(p, 36) * 0.1f);
  // 0x0224 PV charge I
  this->publish_state_(this->pv_charge_current_sensor_, get_u16(p, 40) * 0.1f);
}
```

Extend `dump_config` to log the new sensors:

```cpp
  LOG_SENSOR("  ", "Bus V", this->bus_voltage_sensor_);
  LOG_SENSOR("  ", "Grid V", this->grid_voltage_sensor_);
  LOG_SENSOR("  ", "Inverter V", this->inverter_voltage_sensor_);
  LOG_SENSOR("  ", "Load W", this->load_active_power_sensor_);
```

- [ ] **Step 4: Verify**

Add the block B sensors to `/tmp/srne-validate-3.yaml` under the existing `sensor:` block:

```yaml
    bus_voltage: { name: "Bus V" }
    grid_voltage: { name: "Grid V" }
    grid_current: { name: "Grid A" }
    grid_frequency: { name: "Grid Hz" }
    inverter_voltage: { name: "Inverter V" }
    inverter_current: { name: "Inverter A" }
    inverter_frequency: { name: "Inverter Hz" }
    load_current: { name: "Load A" }
    load_active_power: { name: "Load W" }
    load_apparent_power: { name: "Load VA" }
    load_percent: { name: "Load %" }
    battery_charge_current: { name: "Batt Charge A" }
    pv_charge_current: { name: "PV Charge A" }
    heatsink_a_temperature: { name: "Heatsink A" }
    heatsink_b_temperature: { name: "Heatsink B" }
    heatsink_c_temperature: { name: "Heatsink C" }
```

Run: `cd /home/rar/esphome-srne-inverter && esphome config /tmp/srne-validate-3.yaml 2>&1 | tail -30`
Expected: validates, all 27 sensors present.

- [ ] **Step 5: Commit**

```bash
git add components/srne_inverter/
git commit -m "feat(srne_inverter): add grid/inverter/load/heatsink sensors (block B)"
```

---

## Task 7: Add binary_sensor.py and block C (fault polling + decode)

Add online_status, grid_present, inverter_on, fault as binary sensors. This requires the block C fault poll AND access to the most-recently-seen machine_state register from block B (which we now need to cache as a uint16 for the binary sensor + later text sensor).

**Files:**
- Create: `components/srne_inverter/binary_sensor.py`
- Modify: `components/srne_inverter/srne_inverter.h`
- Modify: `components/srne_inverter/srne_inverter.cpp`

- [ ] **Step 1: Write `components/srne_inverter/binary_sensor.py`**

```python
import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID

DEPENDENCIES = ["srne_inverter"]

CONF_ONLINE_STATUS = "online_status"
CONF_GRID_PRESENT = "grid_present"
CONF_INVERTER_ON = "inverter_on"
CONF_FAULT = "fault"

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_ONLINE_STATUS): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_GRID_PRESENT): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_INVERTER_ON): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_FAULT): binary_sensor.binary_sensor_schema(),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    mapping = {
        CONF_ONLINE_STATUS: hub.set_online_status_binary_sensor,
        CONF_GRID_PRESENT: hub.set_grid_present_binary_sensor,
        CONF_INVERTER_ON: hub.set_inverter_on_binary_sensor,
        CONF_FAULT: hub.set_fault_binary_sensor,
    }
    for key, setter in mapping.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(setter(sens))
```

- [ ] **Step 2: Extend `srne_inverter.h` — binary_sensor include, setters, storage, helpers**

Add at the top of includes:

```cpp
#include "esphome/components/binary_sensor/binary_sensor.h"
```

In the public section after the block B setters, add:

```cpp
  void set_online_status_binary_sensor(binary_sensor::BinarySensor *s) { online_status_binary_sensor_ = s; }
  void set_grid_present_binary_sensor(binary_sensor::BinarySensor *s) { grid_present_binary_sensor_ = s; }
  void set_inverter_on_binary_sensor(binary_sensor::BinarySensor *s) { inverter_on_binary_sensor_ = s; }
  void set_fault_binary_sensor(binary_sensor::BinarySensor *s) { fault_binary_sensor_ = s; }
```

In the protected section after the heatsink sensor storage, add:

```cpp
  binary_sensor::BinarySensor *online_status_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *grid_present_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *inverter_on_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *fault_binary_sensor_{nullptr};

  uint8_t no_response_count_{0};

  void publish_state_(binary_sensor::BinarySensor *s, bool state);
  void decode_block_c_(const uint8_t *payload, size_t byte_count);
```

- [ ] **Step 3: Extend `srne_inverter.cpp` — block C constants, decoder, online watchdog**

Add after block B constants:

```cpp
// Block C: faults — 0x0200..0x0207 (8 regs, 16 bytes)
static const uint16_t REG_BLOCK_C_START = 0x0200;
static const uint16_t REG_BLOCK_C_COUNT = 0x08;
static const uint8_t BLOCK_C_BYTE_COUNT = 0x10;

static const uint8_t MAX_NO_RESPONSE_COUNT = 5;
```

Replace `update()` to also issue block C and to manage the watchdog:

```cpp
void SrneInverter::update() {
  if (this->no_response_count_ >= MAX_NO_RESPONSE_COUNT) {
    if (this->online_status_binary_sensor_ != nullptr) {
      this->online_status_binary_sensor_->publish_state(false);
    }
  }
  this->no_response_count_++;

  this->expected_steps_.push(0);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_A_START, REG_BLOCK_A_COUNT);
  this->expected_steps_.push(1);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B_START, REG_BLOCK_B_COUNT);
  this->expected_steps_.push(2);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_C_START, REG_BLOCK_C_COUNT);
}
```

Extend the `switch (step)` in `on_modbus_data` to include case 2:

```cpp
    case 2:
      if (byte_count == BLOCK_C_BYTE_COUNT) this->decode_block_c_(payload, byte_count);
      break;
```

After `if (address != this->address_) return;` add the watchdog reset:

```cpp
  this->no_response_count_ = 0;
  if (this->online_status_binary_sensor_ != nullptr) {
    this->online_status_binary_sensor_->publish_state(true);
  }
```

Add `inverter_on` and `grid_present` to `decode_block_b_`. At the top of that function (before any `publish_state_` calls):

```cpp
  uint16_t machine_state = get_u16(p, 0);
  uint16_t grid_voltage_raw = get_u16(p, 6);

  // grid_present: heuristic, true when grid V > 50.0 V
  this->publish_state_(this->grid_present_binary_sensor_, grid_voltage_raw > 500);

  // inverter_on: machine_state == 5 (Inverter powered) or 7 (Mains->Inverter)
  bool inverter_on = (machine_state == 5) || (machine_state == 7);
  this->publish_state_(this->inverter_on_binary_sensor_, inverter_on);
```

(Yes — the existing line `grid_voltage_sensor_` uses `get_u16(p, 6)`, so we reuse that.)

Add `decode_block_c_`:

```cpp
void SrneInverter::decode_block_c_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_C_BYTE_COUNT) return;
  // Fault is true if any of 0x0200..0x0203 != 0 (4 regs = 8 bytes)
  bool fault = false;
  for (size_t i = 0; i < 8; i++) {
    if (p[i] != 0) {
      fault = true;
      break;
    }
  }
  this->publish_state_(this->fault_binary_sensor_, fault);
}
```

Add the binary_sensor publish helper:

```cpp
void SrneInverter::publish_state_(binary_sensor::BinarySensor *s, bool state) {
  if (s != nullptr) {
    s->publish_state(state);
  }
}
```

Extend `dump_config` with the binary sensors:

```cpp
  LOG_BINARY_SENSOR("  ", "Online Status", this->online_status_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Grid Present", this->grid_present_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Inverter On", this->inverter_on_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Fault", this->fault_binary_sensor_);
```

- [ ] **Step 4: Verify**

Add to `/tmp/srne-validate-3.yaml`:

```yaml
binary_sensor:
  - platform: srne_inverter
    srne_inverter_id: inv0
    online_status: { name: "Online" }
    grid_present: { name: "Grid Present" }
    inverter_on: { name: "Inverter On" }
    fault: { name: "Fault" }
```

Run: `cd /home/rar/esphome-srne-inverter && esphome config /tmp/srne-validate-3.yaml 2>&1 | tail -20`
Expected: validates, lists the binary sensors.

- [ ] **Step 5: Commit**

```bash
git add components/srne_inverter/
git commit -m "feat(srne_inverter): add binary sensors (online/grid/inverter/fault) and block C fault poll"
```

---

## Task 8: Add text_sensor.py for machine_state, charge_state, fault_codes

Wire up the three runtime text sensors. Product info (versions, serial) come in Task 9.

**Files:**
- Create: `components/srne_inverter/text_sensor.py`
- Modify: `components/srne_inverter/srne_inverter.h`
- Modify: `components/srne_inverter/srne_inverter.cpp`

- [ ] **Step 1: Write `components/srne_inverter/text_sensor.py`**

```python
import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID

DEPENDENCIES = ["srne_inverter"]

CONF_MACHINE_STATE = "machine_state"
CONF_CHARGE_STATE = "charge_state"
CONF_FAULT_CODES = "fault_codes"
CONF_SOFTWARE_VERSION = "software_version"
CONF_HARDWARE_VERSION = "hardware_version"
CONF_SERIAL_NUMBER = "serial_number"

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_MACHINE_STATE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_CHARGE_STATE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_FAULT_CODES): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_SOFTWARE_VERSION): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_HARDWARE_VERSION): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_SERIAL_NUMBER): text_sensor.text_sensor_schema(),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    mapping = {
        CONF_MACHINE_STATE: hub.set_machine_state_text_sensor,
        CONF_CHARGE_STATE: hub.set_charge_state_text_sensor,
        CONF_FAULT_CODES: hub.set_fault_codes_text_sensor,
        CONF_SOFTWARE_VERSION: hub.set_software_version_text_sensor,
        CONF_HARDWARE_VERSION: hub.set_hardware_version_text_sensor,
        CONF_SERIAL_NUMBER: hub.set_serial_number_text_sensor,
    }
    for key, setter in mapping.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(setter(sens))
```

- [ ] **Step 2: Extend `srne_inverter.h` — text_sensor includes, all 6 setters, storage**

Add include:

```cpp
#include "esphome/components/text_sensor/text_sensor.h"
```

Public setters (all 6 — software/hardware/serial are reserved for Task 9 but declared now so the linker resolves):

```cpp
  void set_machine_state_text_sensor(text_sensor::TextSensor *s) { machine_state_text_sensor_ = s; }
  void set_charge_state_text_sensor(text_sensor::TextSensor *s) { charge_state_text_sensor_ = s; }
  void set_fault_codes_text_sensor(text_sensor::TextSensor *s) { fault_codes_text_sensor_ = s; }
  void set_software_version_text_sensor(text_sensor::TextSensor *s) { software_version_text_sensor_ = s; }
  void set_hardware_version_text_sensor(text_sensor::TextSensor *s) { hardware_version_text_sensor_ = s; }
  void set_serial_number_text_sensor(text_sensor::TextSensor *s) { serial_number_text_sensor_ = s; }
```

Protected storage:

```cpp
  text_sensor::TextSensor *machine_state_text_sensor_{nullptr};
  text_sensor::TextSensor *charge_state_text_sensor_{nullptr};
  text_sensor::TextSensor *fault_codes_text_sensor_{nullptr};
  text_sensor::TextSensor *software_version_text_sensor_{nullptr};
  text_sensor::TextSensor *hardware_version_text_sensor_{nullptr};
  text_sensor::TextSensor *serial_number_text_sensor_{nullptr};

  void publish_state_(text_sensor::TextSensor *s, const std::string &state);
  std::string decode_machine_state_(uint16_t state);
  std::string decode_charge_state_(uint16_t state);
```

- [ ] **Step 3: Implement decoders and publishing in `srne_inverter.cpp`**

Add at the bottom of the file (before the namespace close):

```cpp
void SrneInverter::publish_state_(text_sensor::TextSensor *s, const std::string &state) {
  if (s != nullptr) {
    s->publish_state(state);
  }
}

std::string SrneInverter::decode_machine_state_(uint16_t state) {
  switch (state) {
    case 0: return "Power-up delay";
    case 1: return "Waiting";
    case 2: return "Initialization";
    case 3: return "Soft start";
    case 4: return "Mains powered";
    case 5: return "Inverter powered";
    case 6: return "Inverter to Mains";
    case 7: return "Mains to Inverter";
    case 8: return "Battery activate";
    case 9: return "Shutdown by user";
    case 10: return "Fault";
    default: return str_sprintf("Unknown (%u)", state);
  }
}

std::string SrneInverter::decode_charge_state_(uint16_t state) {
  switch (state) {
    case 0: return "Off";
    case 1: return "Quick charge";
    case 2: return "Constant voltage";
    case 3: return "Boost";
    case 4: return "Float";
    case 5: return "Reserved";
    case 6: return "Li activate";
    case 7: return "Reserved";
    default: return str_sprintf("Unknown (%u)", state);
  }
}
```

`str_sprintf` is in `esphome/core/helpers.h` — add `#include "esphome/core/helpers.h"` near the top of `srne_inverter.cpp` if not already present.

In `decode_block_a_`, after the existing decodes, add charge_state publish:

```cpp
  // 0x010B charge_state
  uint16_t charge_state = get_u16(p, 22);
  this->publish_state_(this->charge_state_text_sensor_, this->decode_charge_state_(charge_state));
```

In `decode_block_b_`, after computing `machine_state`, add:

```cpp
  this->publish_state_(this->machine_state_text_sensor_, this->decode_machine_state_(machine_state));
```

In `decode_block_c_`, replace the body with the version that also publishes fault codes:

```cpp
void SrneInverter::decode_block_c_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_C_BYTE_COUNT) return;

  bool fault = false;
  for (size_t i = 0; i < 8; i++) {
    if (p[i] != 0) { fault = true; break; }
  }
  this->publish_state_(this->fault_binary_sensor_, fault);

  // 0x0204..0x0207: 4 fault codes, each 1 register
  std::string codes;
  for (size_t i = 0; i < 4; i++) {
    uint16_t code = get_u16(p, 8 + i * 2);
    if (code != 0) {
      if (!codes.empty()) codes += ";";
      codes += str_sprintf("%u", code);
    }
  }
  if (codes.empty()) codes = "None";
  this->publish_state_(this->fault_codes_text_sensor_, codes);
}
```

- [ ] **Step 4: Verify**

Add to `/tmp/srne-validate-3.yaml`:

```yaml
text_sensor:
  - platform: srne_inverter
    srne_inverter_id: inv0
    machine_state: { name: "Machine State" }
    charge_state: { name: "Charge State" }
    fault_codes: { name: "Fault Codes" }
```

Run: `cd /home/rar/esphome-srne-inverter && esphome config /tmp/srne-validate-3.yaml 2>&1 | tail -20`
Expected: validates.

- [ ] **Step 5: Commit**

```bash
git add components/srne_inverter/
git commit -m "feat(srne_inverter): add machine_state/charge_state/fault_codes text sensors"
```

---

## Task 9: Add slow-update product info (versions + serial)

Schedule blocks D and E once at boot and every ~30 polls afterwards. Decode versions (numeric pairs) and serial number (ASCII per-register-low-byte).

**Files:**
- Modify: `components/srne_inverter/srne_inverter.h`
- Modify: `components/srne_inverter/srne_inverter.cpp`

- [ ] **Step 1: Extend `srne_inverter.h` — counters and decode helpers**

Add to the protected section:

```cpp
  uint32_t update_counter_{0};

  void decode_block_d_(const uint8_t *payload, size_t byte_count);
  void decode_block_e_(const uint8_t *payload, size_t byte_count);
  std::string extract_low_byte_string_(const uint8_t *data, size_t length);
```

- [ ] **Step 2: Extend `srne_inverter.cpp` — block D/E constants, scheduling, decoders**

Add after block C constants:

```cpp
// Block D: software/hardware versions — 0x0014..0x0017 (4 regs, 8 bytes)
static const uint16_t REG_BLOCK_D_START = 0x0014;
static const uint16_t REG_BLOCK_D_COUNT = 0x04;
static const uint8_t BLOCK_D_BYTE_COUNT = 0x08;

// Block E: product SN string — 0x0035..0x0048 (20 regs, 40 bytes)
static const uint16_t REG_BLOCK_E_START = 0x0035;
static const uint16_t REG_BLOCK_E_COUNT = 0x14;
static const uint8_t BLOCK_E_BYTE_COUNT = 0x28;

static const uint32_t PRODUCT_INFO_INTERVAL = 30;  // every 30 update cycles
```

In `update()`, after pushing the three fast-poll blocks, append:

```cpp
  if ((this->update_counter_ % PRODUCT_INFO_INTERVAL) == 0) {
    this->expected_steps_.push(3);
    this->send(FUNCTION_READ_HOLDING, REG_BLOCK_D_START, REG_BLOCK_D_COUNT);
    this->expected_steps_.push(4);
    this->send(FUNCTION_READ_HOLDING, REG_BLOCK_E_START, REG_BLOCK_E_COUNT);
  }
  this->update_counter_++;
```

Add cases 3 and 4 to the dispatch switch in `on_modbus_data`:

```cpp
    case 3:
      if (byte_count == BLOCK_D_BYTE_COUNT) this->decode_block_d_(payload, byte_count);
      break;
    case 4:
      if (byte_count == BLOCK_E_BYTE_COUNT) this->decode_block_e_(payload, byte_count);
      break;
```

Add decoders:

```cpp
void SrneInverter::decode_block_d_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_D_BYTE_COUNT) return;
  // 0x0014..0x0015 software (CPU1/CPU2), 0x0016..0x0017 hardware (control/power)
  // Per PDF: e.g. value 100 → "v1.00"
  auto fmt_ver = [](uint16_t v) {
    return str_sprintf("v%u.%02u", v / 100, v % 100);
  };
  uint16_t cpu1 = get_u16(p, 0);
  uint16_t cpu2 = get_u16(p, 2);
  uint16_t ctrl = get_u16(p, 4);
  uint16_t pwr = get_u16(p, 6);
  this->publish_state_(this->software_version_text_sensor_,
                       "CPU1 " + fmt_ver(cpu1) + " / CPU2 " + fmt_ver(cpu2));
  this->publish_state_(this->hardware_version_text_sensor_,
                       "Control " + fmt_ver(ctrl) + " / Power " + fmt_ver(pwr));
}

void SrneInverter::decode_block_e_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_E_BYTE_COUNT) return;
  this->publish_state_(this->serial_number_text_sensor_,
                       this->extract_low_byte_string_(p, byte_count));
}

std::string SrneInverter::extract_low_byte_string_(const uint8_t *data, size_t length) {
  // PDF says "String format, low 8 bits per register valid, high 8 bits invalid".
  // Each register = 2 bytes (big-endian on wire), so the valid byte is the second of each pair.
  std::string result;
  result.reserve(length / 2);
  for (size_t i = 1; i < length; i += 2) {
    uint8_t c = data[i];
    if (c >= 0x20 && c <= 0x7E) {
      result += static_cast<char>(c);
    } else if (c == 0x00) {
      break;
    }
  }
  while (!result.empty() && result.back() == ' ') result.pop_back();
  return result;
}
```

Extend `dump_config` to log the text sensors:

```cpp
  LOG_TEXT_SENSOR("  ", "Machine State", this->machine_state_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Charge State", this->charge_state_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Software", this->software_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Hardware", this->hardware_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Serial", this->serial_number_text_sensor_);
```

- [ ] **Step 3: Verify**

Add to `/tmp/srne-validate-3.yaml` under `text_sensor:`:

```yaml
    software_version: { name: "Software" }
    hardware_version: { name: "Hardware" }
    serial_number: { name: "Serial" }
```

Run: `cd /home/rar/esphome-srne-inverter && esphome config /tmp/srne-validate-3.yaml 2>&1 | tail -20`
Expected: validates.

- [ ] **Step 4: Commit**

```bash
git add components/srne_inverter/
git commit -m "feat(srne_inverter): poll product info (versions, serial) every 30 cycles"
```

---

## Task 10: Compile end-to-end (catch any C++ issues)

`esphome config` only runs Python codegen — it does not compile C++. Run a real compile against the example YAML now to catch any C++ errors.

- [ ] **Step 1: Run a full compile**

Run: `cd /home/rar/esphome-srne-inverter && esphome compile /tmp/srne-validate-3.yaml 2>&1 | tail -50`
Expected: completes with `INFO Successfully compiled program.` (will take 2-5 minutes on first run while ESP-IDF is fetched).

If errors occur:
- Fix in the source files
- Re-run `esphome compile`
- Repeat until clean
- Each fix is its own commit with `fix(srne_<file>): <what>` message

- [ ] **Step 2: Commit any fixes** (one commit per fix, conventional commits)

If no fixes were needed, skip.

---

## Task 11: Write example YAML, README, WIRING, REGISTER_MAP

**Files:**
- Create: `esp32-example.yaml`
- Create: `README.md`
- Create: `WIRING.md`
- Create: `REGISTER_MAP.md`

- [ ] **Step 1: Write `esp32-example.yaml`** (matches the spec's example YAML section)

```yaml
substitutions:
  name: srne-inverter
  device_description: "Monitor an SRNE hybrid solar inverter via RS485"
  external_components_source: github://rar/esphome-srne-inverter@main
  tx_pin: GPIO16
  rx_pin: GPIO17

esphome:
  name: ${name}
  comment: ${device_description}
  min_version: 2024.6.0
  project:
    name: "rar.esphome-srne-inverter"
    version: 1.0.0

esp32:
  board: wemos_d1_mini32

external_components:
  - source: ${external_components_source}
    refresh: 0s

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

ota:
  platform: esphome

logger:
  level: DEBUG

api:

uart:
  id: uart_0
  baud_rate: 9600
  tx_pin: ${tx_pin}
  rx_pin: ${rx_pin}
  rx_buffer_size: 256

srne_modbus:
  id: modbus0
  uart_id: uart_0
  # flow_control_pin: GPIO4   # optional; needed for non-auto RS485 transceivers

srne_inverter:
  id: inv0
  srne_modbus_id: modbus0
  address: 0x01
  update_interval: 10s

binary_sensor:
  - platform: srne_inverter
    srne_inverter_id: inv0
    online_status:
      name: "${name} online"
    grid_present:
      name: "${name} grid present"
    inverter_on:
      name: "${name} inverter on"
    fault:
      name: "${name} fault"

sensor:
  - platform: srne_inverter
    srne_inverter_id: inv0

    # Battery
    battery_soc: { name: "${name} battery SOC" }
    battery_voltage: { name: "${name} battery voltage" }
    battery_current: { name: "${name} battery current" }

    # PV
    pv1_voltage: { name: "${name} pv1 voltage" }
    pv1_current: { name: "${name} pv1 current" }
    pv1_power: { name: "${name} pv1 power" }
    pv2_voltage: { name: "${name} pv2 voltage" }
    pv2_current: { name: "${name} pv2 current" }
    pv2_power: { name: "${name} pv2 power" }
    pv_total_power: { name: "${name} pv total power" }
    pv_charge_current: { name: "${name} pv charge current" }
    charge_power: { name: "${name} charge power" }

    # Grid
    bus_voltage: { name: "${name} bus voltage" }
    grid_voltage: { name: "${name} grid voltage" }
    grid_current: { name: "${name} grid current" }
    grid_frequency: { name: "${name} grid frequency" }

    # Inverter / load
    inverter_voltage: { name: "${name} inverter voltage" }
    inverter_current: { name: "${name} inverter current" }
    inverter_frequency: { name: "${name} inverter frequency" }
    load_current: { name: "${name} load current" }
    load_active_power: { name: "${name} load power" }
    load_apparent_power: { name: "${name} load apparent power" }
    load_percent: { name: "${name} load percent" }
    battery_charge_current: { name: "${name} battery charge current" }

    # Heat sinks
    heatsink_a_temperature: { name: "${name} heatsink A temp" }
    heatsink_b_temperature: { name: "${name} heatsink B temp" }
    heatsink_c_temperature: { name: "${name} heatsink C temp" }

text_sensor:
  - platform: srne_inverter
    srne_inverter_id: inv0
    machine_state: { name: "${name} state" }
    charge_state: { name: "${name} charge state" }
    fault_codes: { name: "${name} fault codes" }
    software_version: { name: "${name} firmware" }
    hardware_version: { name: "${name} hardware" }
    serial_number: { name: "${name} serial" }
```

- [ ] **Step 2: Write `README.md`**

```markdown
# esphome-srne-inverter

ESPHome external component for SRNE hybrid solar inverters via RS485 / Modbus RTU.

Read-only support for battery, PV, grid, inverter, load, heatsink temperature, machine state, charge state, fault codes, and product info.

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
```

- [ ] **Step 3: Write `WIRING.md`**

```markdown
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
```

- [ ] **Step 4: Write `REGISTER_MAP.md`**

```markdown
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
```

- [ ] **Step 5: Verify the example YAML validates and compiles**

Copy `secrets.yaml.example` to `secrets.yaml` if not present:

```bash
cp secrets.yaml.example secrets.yaml
```

Run: `cd /home/rar/esphome-srne-inverter && esphome config esp32-example.yaml 2>&1 | tail -5`
Expected: validates.

Run: `cd /home/rar/esphome-srne-inverter && esphome compile esp32-example.yaml 2>&1 | tail -10`
Expected: `INFO Successfully compiled program.`

- [ ] **Step 6: Commit**

```bash
git add esp32-example.yaml README.md WIRING.md REGISTER_MAP.md
git commit -m "docs: add README, wiring guide, register map, and example YAML"
```

---

## Self-review notes

- **Coverage:** Every entity in the spec's "Entities" section (sensors / binary_sensors / text_sensors) has a task that adds the schema + the C++ decode. All 5 polling blocks (A/B/C/D/E) from the spec's "Polling state machine" are scheduled and decoded. Online watchdog, fault aggregation, and version formatting are covered.
- **No placeholders:** Every code block is complete. The two YAML schema files are written end-to-end. No "TODO" or "fill in later" markers.
- **Type consistency:** Setter names match across Python (`hub.set_battery_soc_sensor`) and C++ (`set_battery_soc_sensor`). The `expected_steps_` FIFO is declared in Task 6 and used in Tasks 6/7/9 with the same step values. Block byte counts (`0x24`, `0x2A`, `0x10`, `0x08`, `0x28`) match the spec's polling table.
- **Compile gate:** Task 10 is the explicit full-compile gate after pure-Python validation is satisfied. Any C++ issues caught there get follow-up fix-commits, then Task 11 finishes docs + final compile.
