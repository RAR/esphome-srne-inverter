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

// Block B is split because at least one SRNE firmware variant silently
// times out on reads spanning the password-protection status register at
// 0x0211 (no Modbus error, no bytes). 1-reg reads of 0x0210 alone work
// fine, so we poll machine state as B0 and the rest of the inverter-data
// area starting at 0x0212.
//
// Block B0: machine state — 0x0210 alone (1 reg, 2 bytes)
static const uint16_t REG_BLOCK_B0_START = 0x0210;
static const uint16_t REG_BLOCK_B0_COUNT = 0x01;
static const uint8_t BLOCK_B0_BYTE_COUNT = 0x02;

// Block B1: bus/grid/inverter/load — 0x0212..0x021F (14 regs, 28 bytes)
static const uint16_t REG_BLOCK_B1_START = 0x0212;
static const uint16_t REG_BLOCK_B1_COUNT = 0x0E;
static const uint8_t BLOCK_B1_BYTE_COUNT = 0x1C;

// Block B2: heatsinks + PV charge + DC bus rails — 0x0220..0x0229 (10 regs)
// 0x0228 and 0x0229 are undocumented but the register-space scan showed they
// hold the positive/negative DC bus rail voltages (sum ≈ bus_voltage at 0x0212).
static const uint16_t REG_BLOCK_B2_START = 0x0220;
static const uint16_t REG_BLOCK_B2_COUNT = 0x0A;
static const uint8_t BLOCK_B2_BYTE_COUNT = 0x14;

// Block B3: L2 (split-phase / parallel-120 second-leg) data — 0x022A..0x0236
// PDF marks these "specific machine models", and on this inverter they only
// populate when the unit is actively inverting on both legs. Phase C slots
// (0x022B, 0x022D, 0x022F, 0x0231, 0x0233, 0x0235) are 3-phase and stay 0.
static const uint16_t REG_BLOCK_B3_START = 0x022A;
static const uint16_t REG_BLOCK_B3_COUNT = 0x0D;
static const uint8_t BLOCK_B3_BYTE_COUNT = 0x1A;

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

// Block F4: 0xE20C (1 reg) — eco_mode (binary 0/1)
static const uint16_t REG_BLOCK_F4_START = 0xE20C;
static const uint16_t REG_BLOCK_F4_COUNT = 0x01;
static const uint8_t BLOCK_F4_BYTE_COUNT = 0x02;

// Block F5: 0xE21E (1 reg) — AC Output Phase Mode (0=parallel/0°, 2=split/180°)
// Undocumented; identified by diffing scans before/after toggling the
// inverter's "AC Output Phase Mode" menu item.
static const uint16_t REG_BLOCK_F5_START = 0xE21E;
static const uint16_t REG_BLOCK_F5_COUNT = 0x01;
static const uint8_t BLOCK_F5_BYTE_COUNT = 0x02;

// Block F6: 0xE20D..0xE20E (2 regs) — overload_auto_restart + overheat_auto_restart
static const uint16_t REG_BLOCK_F6_START = 0xE20D;
static const uint16_t REG_BLOCK_F6_COUNT = 0x02;
static const uint8_t BLOCK_F6_BYTE_COUNT = 0x04;

// Block G: 0xE004 (1 reg) — Battery type (separate from P05 since we don't need
// the rest of that block for V1).
static const uint16_t REG_BLOCK_G_START = 0xE004;
static const uint16_t REG_BLOCK_G_COUNT = 0x01;
static const uint8_t BLOCK_G_BYTE_COUNT = 0x02;

// Block H1: 0xE20B (1 reg) — AC input voltage range (UPS / APL)
static const uint16_t REG_BLOCK_H1_START = 0xE20B;
static const uint16_t REG_BLOCK_H1_COUNT = 0x01;
static const uint8_t BLOCK_H1_BYTE_COUNT = 0x02;

// Block H2: 0xE210..0xE212 (3 regs) — buzzer_alarm + alarm_when_interrupted (skip) + inverter_to_bypass
static const uint16_t REG_BLOCK_H2_START = 0xE210;
static const uint16_t REG_BLOCK_H2_COUNT = 0x03;
static const uint8_t BLOCK_H2_BYTE_COUNT = 0x06;

// Block H3: 0xE201 (1 reg) — parallel mode (SIG/PAL/2P0/2P1/2P2/3P1/3P2/3P3)
static const uint16_t REG_BLOCK_H3_START = 0xE201;
static const uint16_t REG_BLOCK_H3_COUNT = 0x01;
static const uint8_t BLOCK_H3_BYTE_COUNT = 0x02;

// Block H4: 0xE00F (1 reg) — SOC discharge cutoff (per V1.7 PDF labeled
// "Charge cut-off SOC, discharge cut-off SOC" packed; on this firmware
// the scan value 5 matches the manual's item-59 default 5%).
static const uint16_t REG_BLOCK_H4_START = 0xE00F;
static const uint16_t REG_BLOCK_H4_COUNT = 0x01;
static const uint8_t BLOCK_H4_BYTE_COUNT = 0x02;

// Block H5: 0xE01D..0xE020 (4 regs) — SOC charge cutoff / discharge alarm /
// switch to mains / switch to inverter (tentative mapping from scan + manual).
static const uint16_t REG_BLOCK_H5_START = 0xE01D;
static const uint16_t REG_BLOCK_H5_COUNT = 0x04;
static const uint8_t BLOCK_H5_BYTE_COUNT = 0x08;

// Settings registers we may write to (function 0x06)
static const uint16_t REG_OUTPUT_PRIORITY = 0xE204;
static const uint16_t REG_MAINS_CHARGE_CURRENT_LIMIT = 0xE205;
static const uint16_t REG_OUTPUT_VOLTAGE = 0xE208;
static const uint16_t REG_MAX_CHARGE_CURRENT = 0xE20A;
static const uint16_t REG_ECO_MODE = 0xE20C;
static const uint16_t REG_OVERLOAD_AUTO_RESTART = 0xE20D;
static const uint16_t REG_OVERHEAT_AUTO_RESTART = 0xE20E;
static const uint16_t REG_CHARGE_PRIORITY = 0xE20F;
static const uint16_t REG_BATTERY_TYPE = 0xE004;
static const uint16_t REG_AC_INPUT_VOLTAGE_RANGE = 0xE20B;
static const uint16_t REG_BUZZER_ALARM = 0xE210;
static const uint16_t REG_INVERTER_TO_BYPASS = 0xE212;
static const uint16_t REG_PARALLEL_MODE = 0xE201;
static const uint16_t REG_SOC_DISCHARGE_CUTOFF = 0xE00F;
static const uint16_t REG_SOC_CHARGE_CUTOFF = 0xE01D;
static const uint16_t REG_SOC_DISCHARGE_ALARM = 0xE01E;
static const uint16_t REG_SOC_SWITCH_TO_MAINS = 0xE01F;
static const uint16_t REG_SOC_SWITCH_TO_INVERTER = 0xE020;

// Sentinel step id for register-scan replies (anything not in 0..8 normal steps).
static const uint8_t SCAN_STEP = 0xFF;

// Defined here so the on_modbus_data dispatch (which uses get_u16 for the
// single-register F3 block) can see them.
static inline uint16_t get_u16(const uint8_t *p, size_t i) {
  return (uint16_t(p[i]) << 8) | uint16_t(p[i + 1]);
}
static inline int16_t get_i16(const uint8_t *p, size_t i) {
  return (int16_t) get_u16(p, i);
}

// Fault code → symbolic name, per §7.1 of the SRNE 12KW user manual.
// Returns nullptr for unknown codes (so the caller can fall back to a numeric
// label and we can identify and add it later).
static const char *fault_code_name_(uint16_t code) {
  switch (code) {
    case  1: return "BatVoltLow";
    case  2: return "BatOverCurrSw";
    case  3: return "BatOpen";
    case  4: return "BatLowEod";
    case  5: return "BatOverCurrHw";
    case  6: return "BatOverVolt";
    case  7: return "BusOverVoltHw";
    case  8: return "BusOverVoltSw";
    case  9: return "PvVoltHigh";
    case 10: return "PvBoostOCSw";
    case 11: return "PvBoostOCHw";
    case 12: return "SpiCommErr";
    case 13: return "OverloadBypass";
    case 14: return "OverloadInverter";
    case 15: return "AcOverCurrHw";
    case 16: return "AuxDSpReqOffPWM";
    case 17: return "InvShort";
    case 18: return "BusSoftFailed";
    case 19: return "OverTemperMppt";
    case 20: return "OverTemperInv";
    case 21: return "FanFail";
    case 22: return "EEPROM";
    case 23: return "ModelNumErr";
    case 24: return "BusDiff";
    case 25: return "BusShort";
    case 26: return "RlyShort";
    case 28: return "LinePhaseErr";
    case 29: return "BusVoltLow";
    case 30: return "BatCapacityLow1";
    case 31: return "BatCapacityLow2";
    case 32: return "BatCapacityLowStop";
    case 34: return "CanCommFault";
    case 35: return "ParaAddrErr";
    case 36: return "BalanceCurrentOC";
    case 37: return "ParaShareCurrErr";
    case 38: return "ParaBattVoltDiff";
    case 39: return "ParaAcSrcDiff";
    case 40: return "ParaHwSynErr";
    case 41: return "InvDcVoltErr";
    case 42: return "SysFwVersionDiff";
    case 43: return "ParaLineContErr";
    case 44: return "SerialNumberError";
    case 45: return "SplitPhaseModeSettingErr";
    case 56: return "LowInsulationResistance";
    case 57: return "LeakageCurrentOverload";
    case 58: return "BMSComErr";
    // 59 undocumented in §7.1; observed on Anenji 12KW when BMS-reported SOC
    // reaches 100% → BMS signals "stop charging". Treat as informational.
    case 59: return "BMSChargeDisabled";
    case 60: return "BMSUnderTem";
    case 61: return "BMSOverTem";
    case 62: return "BMSOverCur";
    case 63: return "BMSUnderVolt";
    case 64: return "BMSOverVolt";
    default: return nullptr;
  }
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
  LOG_BINARY_SENSOR("  ", "Inverter On Load", this->inverter_on_load_binary_sensor_);
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

void SrneInverter::setup() {
  if (this->scan_on_boot_) {
    this->queue_scan_();
  }
}

void SrneInverter::update() {
  // In scan-on-boot mode skip the normal polling cycle entirely — the scan
  // queued in setup() is the only traffic we generate.
  if (this->scan_on_boot_) {
    if (!this->scan_complete_announced_ && this->scan_regs_in_flight_.empty() && this->scan_total_ > 0) {
      ESP_LOGI(TAG, "SCAN COMPLETE: %u/%u registers responded, %u timed out",
               this->scan_responded_, this->scan_total_, this->scan_timed_out_);
      this->scan_complete_announced_ = true;
    }
    return;
  }
  if (this->no_response_count_ >= MAX_NO_RESPONSE_COUNT) {
    if (this->online_status_binary_sensor_ != nullptr) {
      this->online_status_binary_sensor_->publish_state(false);
    }
  }
  this->no_response_count_++;

  // Always-on blocks (the basic readings)
  this->expected_steps_.push(0);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_A_START, REG_BLOCK_A_COUNT);
  this->expected_steps_.push(9);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B0_START, REG_BLOCK_B0_COUNT);
  this->expected_steps_.push(1);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B1_START, REG_BLOCK_B1_COUNT);
  this->expected_steps_.push(2);
  this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B2_START, REG_BLOCK_B2_COUNT);

  // Block B3 (L2 leg) is polled if any per-leg L2 sensor OR any L1+L2
  // combined sensor is configured (the combined sensors are computed inside
  // decode_block_b3_ from cached L1 values).
  bool want_b3 = this->grid_voltage_l2_sensor_ != nullptr ||
                 this->inverter_voltage_l2_sensor_ != nullptr ||
                 this->inverter_current_l2_sensor_ != nullptr ||
                 this->load_current_l2_sensor_ != nullptr ||
                 this->load_active_power_l2_sensor_ != nullptr ||
                 this->load_apparent_power_l2_sensor_ != nullptr ||
                 this->load_percent_l2_sensor_ != nullptr ||
                 this->inverter_voltage_l1_l2_sensor_ != nullptr ||
                 this->inverter_current_total_sensor_ != nullptr ||
                 this->load_current_total_sensor_ != nullptr ||
                 this->load_active_power_total_sensor_ != nullptr ||
                 this->load_apparent_power_total_sensor_ != nullptr;
  if (want_b3) {
    this->expected_steps_.push(11);
    this->send(FUNCTION_READ_HOLDING, REG_BLOCK_B3_START, REG_BLOCK_B3_COUNT);
  }

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
    bool want_f4 = this->eco_mode_switch_ != nullptr;
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
    if (want_f4) {
      this->expected_steps_.push(10);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_F4_START, REG_BLOCK_F4_COUNT);
    }
    // Block F5 (phase mode) needed for split_phase_mode binary sensor AND for
    // mode-aware inverter_voltage_l1_l2 derivation.
    bool want_f5 = this->split_phase_mode_binary_sensor_ != nullptr ||
                   this->inverter_voltage_l1_l2_sensor_ != nullptr;
    if (want_f5) {
      this->expected_steps_.push(12);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_F5_START, REG_BLOCK_F5_COUNT);
    }
    // Block F6 (auto-restart switches)
    bool want_f6 = this->overload_auto_restart_switch_ != nullptr ||
                   this->overheat_auto_restart_switch_ != nullptr;
    if (want_f6) {
      this->expected_steps_.push(13);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_F6_START, REG_BLOCK_F6_COUNT);
    }
    // Block G (battery type) — lives in the P05 settings range, not P07
    bool want_g = this->battery_type_select_ != nullptr;
    if (want_g) {
      this->expected_steps_.push(14);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_G_START, REG_BLOCK_G_COUNT);
    }
    // Block H1: ac_input_voltage_range
    if (this->ac_input_voltage_range_select_ != nullptr) {
      this->expected_steps_.push(15);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_H1_START, REG_BLOCK_H1_COUNT);
    }
    // Block H2: buzzer_alarm + inverter_to_bypass (skip middle reg 0xE211)
    if (this->buzzer_alarm_switch_ != nullptr || this->inverter_to_bypass_switch_ != nullptr) {
      this->expected_steps_.push(16);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_H2_START, REG_BLOCK_H2_COUNT);
    }
    // Block H3: parallel_mode
    if (this->parallel_mode_select_ != nullptr) {
      this->expected_steps_.push(17);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_H3_START, REG_BLOCK_H3_COUNT);
    }
    // Block H4: SOC discharge cutoff
    if (this->soc_discharge_cutoff_number_ != nullptr) {
      this->expected_steps_.push(18);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_H4_START, REG_BLOCK_H4_COUNT);
    }
    // Block H5: the other 4 SOC thresholds
    if (this->soc_charge_cutoff_number_ != nullptr ||
        this->soc_discharge_alarm_number_ != nullptr ||
        this->soc_switch_to_mains_number_ != nullptr ||
        this->soc_switch_to_inverter_number_ != nullptr) {
      this->expected_steps_.push(19);
      this->send(FUNCTION_READ_HOLDING, REG_BLOCK_H5_START, REG_BLOCK_H5_COUNT);
    }
  }
  this->update_counter_++;
}

void SrneInverter::on_modbus_timeout() {
  if (this->expected_steps_.empty()) return;
  uint8_t step = this->expected_steps_.front();
  this->expected_steps_.pop();
  if (step == SCAN_STEP) {
    if (!this->scan_regs_in_flight_.empty()) {
      uint16_t reg = this->scan_regs_in_flight_.front();
      this->scan_regs_in_flight_.pop();
      ESP_LOGI(TAG, "SCAN 0x%04X: TIMEOUT", reg);
      this->scan_timed_out_++;
    }
  } else {
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
    uint8_t err_code = data[2];
    uint8_t step = 0xFE;
    if (!this->expected_steps_.empty()) {
      step = this->expected_steps_.front();
      this->expected_steps_.pop();
    }
    if (step == SCAN_STEP) {
      // Pop the parallel scan queue too so subsequent SCAN labels stay aligned,
      // and surface the failing register address — far more useful than a bare
      // "Modbus error".
      if (!this->scan_regs_in_flight_.empty()) {
        uint16_t reg = this->scan_regs_in_flight_.front();
        this->scan_regs_in_flight_.pop();
        ESP_LOGI(TAG, "SCAN 0x%04X: ERROR 0x%02X (illegal address / not exposed)", reg, err_code);
        this->scan_timed_out_++;
      } else {
        ESP_LOGW(TAG, "SCAN ERROR 0x%02X but scan queue is empty", err_code);
      }
    } else {
      const char *err_name;
      switch (err_code) {
        case 0x01: err_name = "illegal function"; break;
        case 0x02: err_name = "illegal data address"; break;
        case 0x03: err_name = "illegal data value"; break;
        case 0x04: err_name = "device failure"; break;
        case 0x05: err_name = "password check address wrong"; break;
        case 0x07: err_name = "parameter is read-only"; break;
        case 0x08: err_name = "parameter cannot be changed while running"; break;
        case 0x09: err_name = "user password set but not unlocked (write 0xE203)"; break;
        case 0x0B: err_name = "permission denied"; break;
        default:   err_name = "unknown"; break;
      }
      uint8_t  in_fn  = this->parent_->get_in_flight_function();
      uint16_t in_reg = this->parent_->get_in_flight_register();
      ESP_LOGW(TAG, "Modbus error 0x%02X (%s) on fn=0x%02X reg=0x%04X",
               err_code, err_name, in_fn, in_reg);
    }
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

  if (step == SCAN_STEP) {
    if (!this->scan_regs_in_flight_.empty()) {
      uint16_t reg = this->scan_regs_in_flight_.front();
      this->scan_regs_in_flight_.pop();
      this->log_scan_response_(reg, payload, byte_count);
      this->scan_responded_++;
    }
    return;
  }

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
    case 9:
      if (byte_count == BLOCK_B0_BYTE_COUNT) this->decode_block_b0_(payload, byte_count);
      break;
    case 10:
      if (byte_count == BLOCK_F4_BYTE_COUNT) this->decode_block_f4_(payload, byte_count);
      break;
    case 11:
      if (byte_count == BLOCK_B3_BYTE_COUNT) this->decode_block_b3_(payload, byte_count);
      break;
    case 12:
      if (byte_count == BLOCK_F5_BYTE_COUNT) this->decode_block_f5_(payload, byte_count);
      break;
    case 13:
      if (byte_count == BLOCK_F6_BYTE_COUNT) this->decode_block_f6_(payload, byte_count);
      break;
    case 14:
      if (byte_count == BLOCK_G_BYTE_COUNT) this->decode_block_g_(payload, byte_count);
      break;
    case 15:
      if (byte_count == BLOCK_H1_BYTE_COUNT) this->decode_block_h1_(payload, byte_count);
      break;
    case 16:
      if (byte_count == BLOCK_H2_BYTE_COUNT) this->decode_block_h2_(payload, byte_count);
      break;
    case 17:
      if (byte_count == BLOCK_H3_BYTE_COUNT) this->decode_block_h3_(payload, byte_count);
      break;
    case 18:
      if (byte_count == BLOCK_H4_BYTE_COUNT) this->decode_block_h4_(payload, byte_count);
      break;
    case 19:
      if (byte_count == BLOCK_H5_BYTE_COUNT) this->decode_block_h5_(payload, byte_count);
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

void SrneInverter::decode_block_b0_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_B0_BYTE_COUNT) return;

  // 0x0210 machine state — single register on its own because reads spanning
  // 0x0211 (password protection mark) silently time out on this firmware.
  uint16_t machine_state = get_u16(p, 0);
  this->publish_state_(this->machine_state_text_sensor_, this->decode_machine_state_(machine_state));

  // Authoritative inverter_on_load: state == 5 (Inverter powered) or 7
  // (Mains->Inverter). On any other state the load is fed from grid or the
  // inverter is in soft-start/standby/fault — i.e. NOT "on load".
  // Overrides the voltage-based fallback in decode_block_b1_.
  bool inverter_on_load = (machine_state == 5) || (machine_state == 7);
  this->publish_state_(this->inverter_on_load_binary_sensor_, inverter_on_load);
}

void SrneInverter::decode_block_b1_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_B1_BYTE_COUNT) return;

  // Offsets from 0x0212 (this block covers 0x0212..0x021F)
  // 0x0212 bus V, 0x0213 grid V, 0x0214 grid I, 0x0215 grid Hz x0.01
  uint16_t grid_voltage_raw = get_u16(p, 2);
  (void) get_u16(p, 8);  // inverter voltage, now decoded below for reuse
  this->publish_state_(this->bus_voltage_sensor_, get_u16(p, 0) * 0.1f);
  this->publish_state_(this->grid_voltage_sensor_, grid_voltage_raw * 0.1f);
  this->publish_state_(this->grid_current_sensor_, get_u16(p, 4) * 0.1f);
  this->publish_state_(this->grid_frequency_sensor_, get_u16(p, 6) * 0.01f);

  // grid_present: heuristic, true when grid V > 50.0 V
  this->publish_state_(this->grid_present_binary_sensor_, grid_voltage_raw > 500);

  // 0x0216 inverter V, 0x0217 inverter I, 0x0218 inverter Hz x0.01
  this->l1_inverter_voltage_ = get_u16(p, 8) * 0.1f;
  this->l1_inverter_current_ = get_u16(p, 10) * 0.1f;
  this->publish_state_(this->inverter_voltage_sensor_, this->l1_inverter_voltage_);
  this->publish_state_(this->inverter_current_sensor_, this->l1_inverter_current_);
  this->publish_state_(this->inverter_frequency_sensor_, get_u16(p, 12) * 0.01f);
  // 0x0219 load I, 0x021A load PF (gray skip), 0x021B load W, 0x021C load VA, 0x021D DC component (gray skip)
  this->l1_load_current_ = get_u16(p, 14) * 0.1f;
  this->l1_load_active_power_ = (float) get_u16(p, 18);
  this->l1_load_apparent_power_ = (float) get_u16(p, 20);
  this->publish_state_(this->load_current_sensor_, this->l1_load_current_);
  this->publish_state_(this->load_active_power_sensor_, this->l1_load_active_power_);
  this->publish_state_(this->load_apparent_power_sensor_, this->l1_load_apparent_power_);
  // 0x021E battery charge I, 0x021F load %
  this->publish_state_(this->battery_charge_current_sensor_, get_u16(p, 24) * 0.1f);
  this->publish_state_(this->load_percent_sensor_, (float) get_u16(p, 26));
}

void SrneInverter::decode_block_b2_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_B2_BYTE_COUNT) return;

  // Offsets from 0x0220 (this block covers 0x0220..0x0229)
  // 0x0220-0x0222 heatsinks A/B/C (signed, x0.1), 0x0223 D (gray skip), 0x0224 PV charge I,
  // 0x0225-0x0227 unused, 0x0228 +DC bus rail, 0x0229 -DC bus rail (both x0.1, undocumented
  // but identified empirically — sum ≈ bus_voltage at 0x0212)
  this->publish_state_(this->heatsink_a_temperature_sensor_, get_i16(p, 0) * 0.1f);
  this->publish_state_(this->heatsink_b_temperature_sensor_, get_i16(p, 2) * 0.1f);
  this->publish_state_(this->heatsink_c_temperature_sensor_, get_i16(p, 4) * 0.1f);
  this->publish_state_(this->pv_charge_current_sensor_, get_u16(p, 8) * 0.1f);
  this->publish_state_(this->dc_bus_positive_voltage_sensor_, get_u16(p, 16) * 0.1f);
  this->publish_state_(this->dc_bus_negative_voltage_sensor_, get_u16(p, 18) * 0.1f);
}

void SrneInverter::decode_block_c_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_C_BYTE_COUNT) return;

  // Fault is true if any of 0x0200..0x0203 != 0 (4 regs = 8 bytes)
  bool fault = false;
  for (size_t i = 0; i < 8; i++) {
    if (p[i] != 0) { fault = true; break; }
  }
  this->publish_state_(this->fault_binary_sensor_, fault);

  // 0x0204..0x0207: 4 active fault codes, each 1 register. Each decoded to
  // the symbolic name from the inverter's user manual; unknown codes fall
  // back to the raw number so we can identify and add them later.
  std::string codes;
  for (size_t i = 0; i < 4; i++) {
    uint16_t code = get_u16(p, 8 + i * 2);
    if (code != 0) {
      if (!codes.empty()) codes += ";";
      const char *name = fault_code_name_(code);
      if (name != nullptr) {
        codes += name;
      } else {
        codes += str_sprintf("Fault%u", code);
      }
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

void SrneInverter::decode_block_b3_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_B3_BYTE_COUNT) return;

  // Offsets from 0x022A (block covers 0x022A..0x0236, 13 regs)
  // 0x022A grid V L2, 0x022B grid V C (skip), 0x022C inv V L2, 0x022D inv V C (skip)
  // 0x022E inv I L2, 0x022F inv I C (skip), 0x0230 load I L2, 0x0231 load I C (skip)
  // 0x0232 load W L2, 0x0233 load W C (skip), 0x0234 load VA L2, 0x0235 load VA C (skip)
  // 0x0236 load % L2
  float v_l2 = get_u16(p, 4) * 0.1f;
  float i_l2 = get_u16(p, 8) * 0.1f;
  float load_i_l2 = get_u16(p, 12) * 0.1f;
  float load_w_l2 = (float) get_u16(p, 16);
  float load_va_l2 = (float) get_u16(p, 20);

  this->publish_state_(this->grid_voltage_l2_sensor_, get_u16(p, 0) * 0.1f);
  this->publish_state_(this->inverter_voltage_l2_sensor_, v_l2);
  this->publish_state_(this->inverter_current_l2_sensor_, i_l2);
  this->publish_state_(this->load_current_l2_sensor_, load_i_l2);
  this->publish_state_(this->load_active_power_l2_sensor_, load_w_l2);
  this->publish_state_(this->load_apparent_power_l2_sensor_, load_va_l2);
  this->publish_state_(this->load_percent_l2_sensor_, (float) get_u16(p, 24));

  // Combined L1+L2. Voltage depends on phase mode (read from 0xE21E):
  //   Split (180°): line-to-line = L1 + L2  (≈ 240V)
  //   Parallel (0°): line-to-line = |L1 - L2|  (≈ 0V if balanced)
  // We only publish if the mode is known so the value is never misleading.
  if (!std::isnan(this->l1_inverter_voltage_)) {
    float v_l1_l2 = NAN;
    if (this->phase_mode_ == PhaseMode::Split) {
      v_l1_l2 = this->l1_inverter_voltage_ + v_l2;
    } else if (this->phase_mode_ == PhaseMode::Parallel) {
      v_l1_l2 = std::abs(this->l1_inverter_voltage_ - v_l2);
    }
    this->publish_state_(this->inverter_voltage_l1_l2_sensor_, v_l1_l2);
  }
  if (!std::isnan(this->l1_inverter_current_)) {
    this->publish_state_(this->inverter_current_total_sensor_, this->l1_inverter_current_ + i_l2);
  }
  if (!std::isnan(this->l1_load_current_)) {
    this->publish_state_(this->load_current_total_sensor_, this->l1_load_current_ + load_i_l2);
  }
  if (!std::isnan(this->l1_load_active_power_)) {
    this->publish_state_(this->load_active_power_total_sensor_, this->l1_load_active_power_ + load_w_l2);
  }
  if (!std::isnan(this->l1_load_apparent_power_)) {
    this->publish_state_(this->load_apparent_power_total_sensor_, this->l1_load_apparent_power_ + load_va_l2);
  }
}

void SrneInverter::decode_block_f4_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_F4_BYTE_COUNT) return;
  // 0xE20C eco_mode (1 reg, 0=off / 1=on)
  if (this->eco_mode_switch_ != nullptr) {
    static_cast<SrneSwitch *>(this->eco_mode_switch_)->publish_from_raw(get_u16(p, 0));
  }
}

void SrneInverter::decode_block_f6_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_F6_BYTE_COUNT) return;
  // 0xE20D overload_auto_restart, 0xE20E overheat_auto_restart
  if (this->overload_auto_restart_switch_ != nullptr) {
    static_cast<SrneSwitch *>(this->overload_auto_restart_switch_)->publish_from_raw(get_u16(p, 0));
  }
  if (this->overheat_auto_restart_switch_ != nullptr) {
    static_cast<SrneSwitch *>(this->overheat_auto_restart_switch_)->publish_from_raw(get_u16(p, 2));
  }
}

void SrneInverter::decode_block_g_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_G_BYTE_COUNT) return;
  // 0xE004 battery_type (enum)
  if (this->battery_type_select_ != nullptr) {
    static_cast<SrneSelect *>(this->battery_type_select_)->publish_from_raw(get_u16(p, 0));
  }
}

void SrneInverter::decode_block_h1_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_H1_BYTE_COUNT) return;
  if (this->ac_input_voltage_range_select_ != nullptr) {
    static_cast<SrneSelect *>(this->ac_input_voltage_range_select_)->publish_from_raw(get_u16(p, 0));
  }
}

void SrneInverter::decode_block_h2_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_H2_BYTE_COUNT) return;
  // 0xE210 buzzer_alarm, 0xE211 alarm_when_input_interrupted (skip), 0xE212 inverter_to_bypass
  if (this->buzzer_alarm_switch_ != nullptr) {
    static_cast<SrneSwitch *>(this->buzzer_alarm_switch_)->publish_from_raw(get_u16(p, 0));
  }
  if (this->inverter_to_bypass_switch_ != nullptr) {
    static_cast<SrneSwitch *>(this->inverter_to_bypass_switch_)->publish_from_raw(get_u16(p, 4));
  }
}

void SrneInverter::decode_block_h3_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_H3_BYTE_COUNT) return;
  if (this->parallel_mode_select_ != nullptr) {
    static_cast<SrneSelect *>(this->parallel_mode_select_)->publish_from_raw(get_u16(p, 0));
  }
}

void SrneInverter::decode_block_h4_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_H4_BYTE_COUNT) return;
  if (this->soc_discharge_cutoff_number_ != nullptr) {
    static_cast<SrneNumber *>(this->soc_discharge_cutoff_number_)->publish_from_raw(get_u16(p, 0));
  }
}

void SrneInverter::decode_block_h5_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_H5_BYTE_COUNT) return;
  // 0xE01D charge_cutoff, 0xE01E discharge_alarm, 0xE01F switch_to_mains, 0xE020 switch_to_inverter
  if (this->soc_charge_cutoff_number_ != nullptr) {
    static_cast<SrneNumber *>(this->soc_charge_cutoff_number_)->publish_from_raw(get_u16(p, 0));
  }
  if (this->soc_discharge_alarm_number_ != nullptr) {
    static_cast<SrneNumber *>(this->soc_discharge_alarm_number_)->publish_from_raw(get_u16(p, 2));
  }
  if (this->soc_switch_to_mains_number_ != nullptr) {
    static_cast<SrneNumber *>(this->soc_switch_to_mains_number_)->publish_from_raw(get_u16(p, 4));
  }
  if (this->soc_switch_to_inverter_number_ != nullptr) {
    static_cast<SrneNumber *>(this->soc_switch_to_inverter_number_)->publish_from_raw(get_u16(p, 6));
  }
}

void SrneInverter::decode_block_f5_(const uint8_t *p, size_t byte_count) {
  if (byte_count < BLOCK_F5_BYTE_COUNT) return;
  // 0xE21E AC Output Phase Mode: 0=parallel(0°), 2=split(180°)
  uint16_t raw = get_u16(p, 0);
  switch (raw) {
    case 0: this->phase_mode_ = PhaseMode::Parallel; break;
    case 2: this->phase_mode_ = PhaseMode::Split; break;
    default:
      this->phase_mode_ = PhaseMode::Unknown;
      ESP_LOGW(TAG, "Phase mode register 0xE21E returned unexpected value %u", raw);
      break;
  }
  if (this->split_phase_mode_binary_sensor_ != nullptr) {
    this->split_phase_mode_binary_sensor_->publish_state(this->phase_mode_ == PhaseMode::Split);
  }
}

// --- Register-space scan ---

void SrneInverter::queue_scan_() {
  struct Range { uint16_t start; uint16_t end; const char *label; };
  static const Range ranges[] = {
    {0x0014, 0x004A, "P00 product info"},
    {0x0100, 0x0125, "P01 controller / PV"},
    {0x0200, 0x0250, "P02 inverter data (incl. phase B/C span)"},
    {0xE000, 0xE040, "P05 battery settings"},
    {0xE200, 0xE220, "P07 inverter settings"},
    {0xF000, 0xF050, "P08 historical (sample)"},
  };
  ESP_LOGI(TAG, "===== REGISTER SCAN STARTING =====");
  for (const auto &r : ranges) {
    ESP_LOGI(TAG, "  range 0x%04X..0x%04X  %s", r.start, r.end, r.label);
    for (uint32_t reg = r.start; reg <= r.end; reg++) {
      this->scan_regs_in_flight_.push(static_cast<uint16_t>(reg));
      this->expected_steps_.push(SCAN_STEP);
      this->send(FUNCTION_READ_HOLDING, static_cast<uint16_t>(reg), 1);
      this->scan_total_++;
    }
  }
  ESP_LOGI(TAG, "===== Queued %u single-register reads (expect a few minutes) =====",
           this->scan_total_);
}

void SrneInverter::log_scan_response_(uint16_t reg, const uint8_t *payload, size_t byte_count) {
  if (byte_count < 2) {
    ESP_LOGI(TAG, "SCAN 0x%04X: short response (%u bytes)", reg, (unsigned) byte_count);
    return;
  }
  uint16_t u = get_u16(payload, 0);
  int16_t s = static_cast<int16_t>(u);
  // ASCII interpretation for string regs (high byte often 0, low byte = char)
  char a_hi = (payload[0] >= 0x20 && payload[0] <= 0x7E) ? (char) payload[0] : '.';
  char a_lo = (payload[1] >= 0x20 && payload[1] <= 0x7E) ? (char) payload[1] : '.';
  ESP_LOGI(TAG, "SCAN 0x%04X: u16=%5u  i16=%6d  hex=%02X %02X  ascii='%c%c'",
           reg, u, s, payload[0], payload[1], a_hi, a_lo);
}

// --- SrneSelect / SrneNumber ---

void SrneSelect::control(const std::string &value) {
  auto &options = this->traits.get_options();
  for (size_t i = 0; i < options.size(); i++) {
    if (options[i] == value) {
      ESP_LOGI("srne_select", "Writing 0x%04X = %u (%s)", this->register_, (unsigned) i, value.c_str());
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
  ESP_LOGI("srne_number", "Writing 0x%04X = %u (%.2f)", this->register_, raw, value);
  this->parent_->write_register(this->register_, raw);
  this->publish_state(value);  // optimistic
}

void SrneNumber::publish_from_raw(uint16_t raw) {
  this->publish_state(static_cast<float>(raw) * this->scale_);
}

void SrneSwitch::write_state(bool state) {
  uint16_t raw = state ? 1 : 0;
  ESP_LOGI("srne_switch", "Writing 0x%04X = %u (%s)", this->register_, raw, state ? "ON" : "OFF");
  this->parent_->write_register(this->register_, raw);
  this->publish_state(state);  // optimistic; next F-block read confirms
}

void SrneSwitch::publish_from_raw(uint16_t raw) {
  this->publish_state(raw != 0);
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
