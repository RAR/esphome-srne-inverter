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

// Block B is split because at least one SRNE firmware variant does not
// expose 0x0210 (machine state) or 0x0211 (password protection status mark);
// reads spanning them silently time out (no Modbus error, no bytes).
// Reads ≤20 regs that start at 0x0212 or later work fine.
//
// Block B1: bus/grid/inverter/load — 0x0212..0x021F (14 regs, 28 bytes)
static const uint16_t REG_BLOCK_B1_START = 0x0212;
static const uint16_t REG_BLOCK_B1_COUNT = 0x0E;
static const uint8_t BLOCK_B1_BYTE_COUNT = 0x1C;

// Block B2: heatsinks + PV charge — 0x0220..0x0224 (5 regs, 10 bytes)
static const uint16_t REG_BLOCK_B2_START = 0x0220;
static const uint16_t REG_BLOCK_B2_COUNT = 0x05;
static const uint8_t BLOCK_B2_BYTE_COUNT = 0x0A;

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

// Block F1: 0xE204..0xE205 (2 regs) — output_priority + mains_charge_current_limit
static const uint16_t REG_BLOCK_F1_START = 0xE204;
static const uint16_t REG_BLOCK_F1_COUNT = 0x02;
static const uint8_t BLOCK_F1_BYTE_COUNT = 0x04;

// Block F2: 0xE208..0xE20A (3 regs) — output_voltage + output_frequency + max_charge_current
// (skips 0xE206 equalizing-enable, 0xE207 power-save-level which is gray on inverters)
static const uint16_t REG_BLOCK_F2_START = 0xE208;
static const uint16_t REG_BLOCK_F2_COUNT = 0x03;
static const uint8_t BLOCK_F2_BYTE_COUNT = 0x06;

// Block F3: 0xE20F (1 reg) — charge_priority (separate because we skip E20B-E20E ranges)
static const uint16_t REG_BLOCK_F3_START = 0xE20F;
static const uint16_t REG_BLOCK_F3_COUNT = 0x01;
static const uint8_t BLOCK_F3_BYTE_COUNT = 0x02;

// Settings registers we may write to (function 0x06)
static const uint16_t REG_OUTPUT_PRIORITY = 0xE204;
static const uint16_t REG_MAINS_CHARGE_CURRENT_LIMIT = 0xE205;
static const uint16_t REG_OUTPUT_VOLTAGE = 0xE208;
static const uint16_t REG_MAX_CHARGE_CURRENT = 0xE20A;
static const uint16_t REG_CHARGE_PRIORITY = 0xE20F;

// Defined here so the on_modbus_data dispatch (which uses get_u16 for the
// single-register F3 block) can see them.
static inline uint16_t get_u16(const uint8_t *p, size_t i) {
  return (uint16_t(p[i]) << 8) | uint16_t(p[i + 1]);
}
static inline int16_t get_i16(const uint8_t *p, size_t i) {
  return (int16_t) get_u16(p, i);
}

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
  if (this->output_priority_select_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Output Priority Select: configured");
  if (this->charge_priority_select_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Charge Priority Select: configured");
  if (this->max_charge_current_number_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Max Charge Current Number: configured");
  if (this->mains_charge_current_limit_number_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Mains Charge Current Limit Number: configured");
  if (this->output_voltage_number_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Output Voltage Number: configured");
}

float SrneInverter::get_setup_priority() const { return setup_priority::DATA; }

void SrneInverter::update() {
  if (this->no_response_count_ >= MAX_NO_RESPONSE_COUNT) {
    if (this->online_status_binary_sensor_ != nullptr) {
      this->online_status_binary_sensor_->publish_state(false);
    }
  }
  this->no_response_count_++;

  // Always-on blocks (the basic readings)
  this->expected_steps_.push(0);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_A_START, REG_BLOCK_A_COUNT);
  this->expected_steps_.push(1);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B1_START, REG_BLOCK_B1_COUNT);
  this->expected_steps_.push(2);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B2_START, REG_BLOCK_B2_COUNT);

  // Block C (faults) is debug-only on some firmware variants and silently
  // times out. Only poll if the user actually configured the fault sensors.
  bool want_c = this->fault_binary_sensor_ != nullptr || this->fault_codes_text_sensor_ != nullptr;
  if (want_c) {
    this->expected_steps_.push(3);
    this->send(FUNCTION_READ_HOLDING, REG_BLOCK_C_START, REG_BLOCK_C_COUNT);
  }

  if ((this->update_counter_ % PRODUCT_INFO_INTERVAL) == 0) {
    bool want_d = this->software_version_text_sensor_ != nullptr ||
                  this->hardware_version_text_sensor_ != nullptr;
    bool want_e = this->serial_number_text_sensor_ != nullptr;
    if (want_d) {
      this->expected_steps_.push(4);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_D_START, REG_BLOCK_D_COUNT);
    }
    if (want_e) {
      this->expected_steps_.push(5);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_E_START, REG_BLOCK_E_COUNT);
    }

    // Read writable settings back so HA shows the current values. Skip
    // entirely if the user didn't configure any of the settings entities.
    bool want_f1 = this->output_priority_select_ != nullptr ||
                   this->mains_charge_current_limit_number_ != nullptr;
    bool want_f2 = this->output_voltage_number_ != nullptr ||
                   this->max_charge_current_number_ != nullptr;
    bool want_f3 = this->charge_priority_select_ != nullptr;
    if (want_f1) {
      this->expected_steps_.push(6);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_F1_START, REG_BLOCK_F1_COUNT);
    }
    if (want_f2) {
      this->expected_steps_.push(7);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_F2_START, REG_BLOCK_F2_COUNT);
    }
    if (want_f3) {
      this->expected_steps_.push(8);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_F3_START, REG_BLOCK_F3_COUNT);
    }
  }
  this->update_counter_++;
}

void SrneInverter::on_modbus_timeout() {
  if (!this->expected_steps_.empty()) {
    uint8_t step = this->expected_steps_.front();
    this->expected_steps_.pop();
    ESP_LOGD(TAG, "Dropping queued step %u after timeout", step);
  }
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
      if (byte_count == BLOCK_B1_BYTE_COUNT) this->decode_block_b1_(payload, byte_count);
      break;
    case 2:
      if (byte_count == BLOCK_B2_BYTE_COUNT) this->decode_block_b2_(payload, byte_count);
      break;
    case 3:
      if (byte_count == BLOCK_C_BYTE_COUNT) this->decode_block_c_(payload, byte_count);
      break;
    case 4:
      if (byte_count == BLOCK_D_BYTE_COUNT) this->decode_block_d_(payload, byte_count);
      break;
    case 5:
      if (byte_count == BLOCK_E_BYTE_COUNT) this->decode_block_e_(payload, byte_count);
      break;
    case 6:
      if (byte_count == BLOCK_F1_BYTE_COUNT) this->decode_block_f1_(payload, byte_count);
      break;
    case 7:
      if (byte_count == BLOCK_F2_BYTE_COUNT) this->decode_block_f2_(payload, byte_count);
      break;
    case 8:
      if (byte_count == BLOCK_F3_BYTE_COUNT) {
        // F3 is a single register — feed it straight into the charge_priority select.
        if (this->charge_priority_select_ != nullptr) {
          static_cast<SrneSelect *>(this->charge_priority_select_)->publish_from_raw(get_u16(payload, 0));
        }
      }
      break;
  }

  // A successful function-0x06 write echoes back [addr, 0x06, reg_hi, reg_lo,
  // val_hi, val_lo, crc, crc]. We don't currently re-publish from the echo —
  // the next F1/F2/F3 read (≤5 min away) is the authoritative re-sync.
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

void SrneInverter::decode_block_b1_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_B1_BYTE_COUNT) return;

  // Offsets from 0x0212 (this block covers 0x0212..0x021F)
  // 0x0212 bus V, 0x0213 grid V, 0x0214 grid I, 0x0215 grid Hz x0.01
  uint16_t grid_voltage_raw = get_u16(p, 2);
  uint16_t inverter_voltage_raw = get_u16(p, 8);
  this->publish_state_(this->bus_voltage_sensor_, get_u16(p, 0) * 0.1f);
  this->publish_state_(this->grid_voltage_sensor_, grid_voltage_raw * 0.1f);
  this->publish_state_(this->grid_current_sensor_, get_u16(p, 4) * 0.1f);
  this->publish_state_(this->grid_frequency_sensor_, get_u16(p, 6) * 0.01f);

  // grid_present: heuristic, true when grid V > 50.0 V
  this->publish_state_(this->grid_present_binary_sensor_, grid_voltage_raw > 500);

  // 0x0216 inverter V, 0x0217 inverter I, 0x0218 inverter Hz x0.01
  this->publish_state_(this->inverter_voltage_sensor_, inverter_voltage_raw * 0.1f);
  this->publish_state_(this->inverter_current_sensor_, get_u16(p, 10) * 0.1f);
  this->publish_state_(this->inverter_frequency_sensor_, get_u16(p, 12) * 0.01f);
  // 0x0219 load I, 0x021A load PF (gray skip), 0x021B load W, 0x021C load VA, 0x021D DC component (gray skip)
  this->publish_state_(this->load_current_sensor_, get_u16(p, 14) * 0.1f);
  this->publish_state_(this->load_active_power_sensor_, (float) get_u16(p, 18));
  this->publish_state_(this->load_apparent_power_sensor_, (float) get_u16(p, 20));
  // 0x021E battery charge I, 0x021F load %
  this->publish_state_(this->battery_charge_current_sensor_, get_u16(p, 24) * 0.1f);
  this->publish_state_(this->load_percent_sensor_, (float) get_u16(p, 26));

  // Fallback derivation of inverter_on since this firmware does not expose
  // the machine state register at 0x0210. Heuristic: inverter is producing
  // AC if its output voltage is plausibly nominal (>40V cuts off boost/idle
  // residuals like the 6.5V cycling we see during standby).
  this->publish_state_(this->inverter_on_binary_sensor_, inverter_voltage_raw > 400);
}

void SrneInverter::decode_block_b2_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_B2_BYTE_COUNT) return;

  // Offsets from 0x0220 (this block covers 0x0220..0x0224)
  // 0x0220-0x0222 heatsinks A/B/C (signed, x0.1), 0x0223 D (gray skip), 0x0224 PV charge I
  this->publish_state_(this->heatsink_a_temperature_sensor_, get_i16(p, 0) * 0.1f);
  this->publish_state_(this->heatsink_b_temperature_sensor_, get_i16(p, 2) * 0.1f);
  this->publish_state_(this->heatsink_c_temperature_sensor_, get_i16(p, 4) * 0.1f);
  this->publish_state_(this->pv_charge_current_sensor_, get_u16(p, 8) * 0.1f);
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

void SrneInverter::decode_block_f1_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_F1_BYTE_COUNT) return;
  // 0xE204 output_priority (1 reg), 0xE205 mains_charge_current_limit (0.1 A)
  uint16_t output_priority = get_u16(p, 0);
  uint16_t mains_limit = get_u16(p, 2);
  if (this->output_priority_select_ != nullptr) {
    static_cast<SrneSelect *>(this->output_priority_select_)->publish_from_raw(output_priority);
  }
  if (this->mains_charge_current_limit_number_ != nullptr) {
    static_cast<SrneNumber *>(this->mains_charge_current_limit_number_)->publish_from_raw(mains_limit);
  }
}

void SrneInverter::decode_block_f2_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_F2_BYTE_COUNT) return;
  // 0xE208 output_voltage (0.1 V), 0xE209 output_frequency (0.01 Hz, not exposed), 0xE20A max_charge_current (0.1 A)
  uint16_t output_voltage = get_u16(p, 0);
  uint16_t max_charge = get_u16(p, 4);
  if (this->output_voltage_number_ != nullptr) {
    static_cast<SrneNumber *>(this->output_voltage_number_)->publish_from_raw(output_voltage);
  }
  if (this->max_charge_current_number_ != nullptr) {
    static_cast<SrneNumber *>(this->max_charge_current_number_)->publish_from_raw(max_charge);
  }
}

// --- SrneSelect / SrneNumber ---

void SrneSelect::control(const std::string &value) {
  auto &options = this->traits.get_options();
  for (size_t i = 0; i < options.size(); i++) {
    if (options[i] == value) {
      ESP_LOGD("srne_select", "Writing 0x%04X = %u (%s)", this->register_, (unsigned) i, value.c_str());
      this->parent_->write_register(this->register_, static_cast<uint16_t>(i));
      this->publish_state(value);  // optimistic; next F-block read confirms
      return;
    }
  }
  ESP_LOGW("srne_select", "Unknown option '%s' for register 0x%04X", value.c_str(), this->register_);
}

void SrneSelect::publish_from_raw(uint16_t raw) {
  auto &options = this->traits.get_options();
  if (raw < options.size()) {
    this->publish_state(options[raw]);
  } else {
    ESP_LOGW("srne_select", "Raw value %u out of range for register 0x%04X (%u options)",
             raw, this->register_, (unsigned) options.size());
  }
}

void SrneNumber::control(float value) {
  // HA-side value → raw register. scale=0.1 means raw = value/0.1 = value*10.
  uint16_t raw = static_cast<uint16_t>((value / this->scale_) + 0.5f);
  ESP_LOGD("srne_number", "Writing 0x%04X = %u (%.2f)", this->register_, raw, value);
  this->parent_->write_register(this->register_, raw);
  this->publish_state(value);  // optimistic
}

void SrneNumber::publish_from_raw(uint16_t raw) {
  this->publish_state(static_cast<float>(raw) * this->scale_);
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
