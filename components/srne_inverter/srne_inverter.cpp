#include "srne_inverter.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace srne_inverter {

static const char *const TAG = "srne_inverter";

static const uint8_t FUNCTION_READ_HOLDING = 0x03;

// Block A: controller / PV — 0x0100..0x0111 (18 regs, 36 bytes)
static const uint16_t REG_BLOCK_A_START = 0x0100;
static const uint16_t REG_BLOCK_A_COUNT = 0x12;
static const uint8_t BLOCK_A_BYTE_COUNT = 0x24;

// Block B: inverter — 0x0210..0x0224 (21 regs, 42 bytes)
static const uint16_t REG_BLOCK_B_START = 0x0210;
static const uint16_t REG_BLOCK_B_COUNT = 0x15;
static const uint8_t BLOCK_B_BYTE_COUNT = 0x2A;

// Block C: faults — 0x0200..0x0207 (8 regs, 16 bytes)
static const uint16_t REG_BLOCK_C_START = 0x0200;
static const uint16_t REG_BLOCK_C_COUNT = 0x08;
static const uint8_t BLOCK_C_BYTE_COUNT = 0x10;

// Block D: software/hardware versions — 0x0014..0x0017 (4 regs, 8 bytes)
static const uint16_t REG_BLOCK_D_START = 0x0014;
static const uint16_t REG_BLOCK_D_COUNT = 0x04;
static const uint8_t BLOCK_D_BYTE_COUNT = 0x08;

// Block E: product SN string — 0x0035..0x0048 (20 regs, 40 bytes)
static const uint16_t REG_BLOCK_E_START = 0x0035;
static const uint16_t REG_BLOCK_E_COUNT = 0x14;
static const uint8_t BLOCK_E_BYTE_COUNT = 0x28;

static const uint8_t MAX_NO_RESPONSE_COUNT = 5;
static const uint32_t PRODUCT_INFO_INTERVAL = 30;  // every N update cycles

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
  LOG_SENSOR("  ", "Bus V", this->bus_voltage_sensor_);
  LOG_SENSOR("  ", "Grid V", this->grid_voltage_sensor_);
  LOG_SENSOR("  ", "Inverter V", this->inverter_voltage_sensor_);
  LOG_SENSOR("  ", "Load W", this->load_active_power_sensor_);
  LOG_BINARY_SENSOR("  ", "Online Status", this->online_status_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Grid Present", this->grid_present_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Inverter On", this->inverter_on_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Fault", this->fault_binary_sensor_);
  LOG_TEXT_SENSOR("  ", "Machine State", this->machine_state_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Charge State", this->charge_state_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Software", this->software_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Hardware", this->hardware_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Serial", this->serial_number_text_sensor_);
}

float SrneInverter::get_setup_priority() const { return setup_priority::DATA; }

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

  if ((this->update_counter_ % PRODUCT_INFO_INTERVAL) == 0) {
    this->expected_steps_.push(3);
    this->send(FUNCTION_READ_HOLDING, REG_BLOCK_D_START, REG_BLOCK_D_COUNT);
    this->expected_steps_.push(4);
    this->send(FUNCTION_READ_HOLDING, REG_BLOCK_E_START, REG_BLOCK_E_COUNT);
  }
  this->update_counter_++;
}

void SrneInverter::on_modbus_data(const std::vector<uint8_t> &data) {
  if (data.size() < 5) return;

  uint8_t address = data[0];
  uint8_t function = data[1];

  if (address != this->address_) return;

  this->no_response_count_ = 0;
  if (this->online_status_binary_sensor_ != nullptr) {
    this->online_status_binary_sensor_->publish_state(true);
  }

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
    case 2:
      if (byte_count == BLOCK_C_BYTE_COUNT) this->decode_block_c_(payload, byte_count);
      break;
    case 3:
      if (byte_count == BLOCK_D_BYTE_COUNT) this->decode_block_d_(payload, byte_count);
      break;
    case 4:
      if (byte_count == BLOCK_E_BYTE_COUNT) this->decode_block_e_(payload, byte_count);
      break;
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
  // 0x010A DC load on/off (skip), 0x010B charge_state
  uint16_t charge_state = get_u16(p, 22);
  this->publish_state_(this->charge_state_text_sensor_, this->decode_charge_state_(charge_state));
  // 0x010C-0x010D fault msg (skip), 0x010E charge_power
  this->publish_state_(this->charge_power_sensor_, (float) get_u16(p, 28));
  // 0x010F PV2 V, 0x0110 PV2 I, 0x0111 PV2 W
  this->publish_state_(this->pv2_voltage_sensor_, get_u16(p, 30) * 0.1f);
  this->publish_state_(this->pv2_current_sensor_, get_u16(p, 32) * 0.1f);
  uint16_t pv2_w = get_u16(p, 34);
  this->publish_state_(this->pv2_power_sensor_, (float) pv2_w);
  this->publish_state_(this->pv_total_power_sensor_, (float) (pv1_w + pv2_w));
}

void SrneInverter::decode_block_b_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_B_BYTE_COUNT) return;

  uint16_t machine_state = get_u16(p, 0);
  uint16_t grid_voltage_raw = get_u16(p, 6);

  this->publish_state_(this->machine_state_text_sensor_, this->decode_machine_state_(machine_state));

  // grid_present: heuristic, true when grid V > 50.0 V
  this->publish_state_(this->grid_present_binary_sensor_, grid_voltage_raw > 500);

  // inverter_on: machine_state == 5 (Inverter powered) or 7 (Mains->Inverter)
  bool inverter_on = (machine_state == 5) || (machine_state == 7);
  this->publish_state_(this->inverter_on_binary_sensor_, inverter_on);

  // Offsets from 0x0210:
  // 0x0210 state (text), 0x0211 password mark (skip)
  // 0x0212 bus V, 0x0213 grid V, 0x0214 grid I, 0x0215 grid Hz x0.01
  this->publish_state_(this->bus_voltage_sensor_, get_u16(p, 4) * 0.1f);
  this->publish_state_(this->grid_voltage_sensor_, grid_voltage_raw * 0.1f);
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

void SrneInverter::decode_block_c_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_C_BYTE_COUNT) return;

  // Fault is true if any of 0x0200..0x0203 != 0 (4 regs = 8 bytes)
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

void SrneInverter::publish_state_(sensor::Sensor *s, float value) {
  if (s != nullptr && !std::isnan(value)) {
    s->publish_state(value);
  }
}

void SrneInverter::publish_state_(binary_sensor::BinarySensor *s, bool state) {
  if (s != nullptr) {
    s->publish_state(state);
  }
}

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
  // PDF: "String format, low 8 bits per register valid, high 8 bits invalid".
  // Each register is 2 bytes (big-endian on wire), so the valid byte is the second of each pair.
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

}  // namespace srne_inverter
}  // namespace esphome
