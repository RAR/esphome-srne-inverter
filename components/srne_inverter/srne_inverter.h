#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/srne_modbus/srne_modbus.h"
#include <queue>

namespace esphome {
namespace srne_inverter {

class SrneInverter : public PollingComponent, public srne_modbus::SrneModbusDevice {
 public:
  void setup() override;
  void loop() override {}
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void on_modbus_data(const std::vector<uint8_t> &data) override;
  void on_modbus_timeout() override;

  void set_scan_on_boot(bool scan) { scan_on_boot_ = scan; }

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

  // Block B sensors
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
  void set_dc_bus_positive_voltage_sensor(sensor::Sensor *s) { dc_bus_positive_voltage_sensor_ = s; }
  void set_dc_bus_negative_voltage_sensor(sensor::Sensor *s) { dc_bus_negative_voltage_sensor_ = s; }

  // L2 (split-phase / parallel-120 second-leg) sensors — Block B3
  void set_grid_voltage_l2_sensor(sensor::Sensor *s) { grid_voltage_l2_sensor_ = s; }
  void set_inverter_voltage_l2_sensor(sensor::Sensor *s) { inverter_voltage_l2_sensor_ = s; }
  void set_inverter_current_l2_sensor(sensor::Sensor *s) { inverter_current_l2_sensor_ = s; }
  void set_load_current_l2_sensor(sensor::Sensor *s) { load_current_l2_sensor_ = s; }
  void set_load_active_power_l2_sensor(sensor::Sensor *s) { load_active_power_l2_sensor_ = s; }
  void set_load_apparent_power_l2_sensor(sensor::Sensor *s) { load_apparent_power_l2_sensor_ = s; }
  void set_load_percent_l2_sensor(sensor::Sensor *s) { load_percent_l2_sensor_ = s; }

  // Binary sensors
  void set_online_status_binary_sensor(binary_sensor::BinarySensor *s) { online_status_binary_sensor_ = s; }
  void set_grid_present_binary_sensor(binary_sensor::BinarySensor *s) { grid_present_binary_sensor_ = s; }
  void set_inverter_on_binary_sensor(binary_sensor::BinarySensor *s) { inverter_on_binary_sensor_ = s; }
  void set_fault_binary_sensor(binary_sensor::BinarySensor *s) { fault_binary_sensor_ = s; }

  // Text sensors
  void set_machine_state_text_sensor(text_sensor::TextSensor *s) { machine_state_text_sensor_ = s; }
  void set_charge_state_text_sensor(text_sensor::TextSensor *s) { charge_state_text_sensor_ = s; }
  void set_fault_codes_text_sensor(text_sensor::TextSensor *s) { fault_codes_text_sensor_ = s; }
  void set_software_version_text_sensor(text_sensor::TextSensor *s) { software_version_text_sensor_ = s; }
  void set_hardware_version_text_sensor(text_sensor::TextSensor *s) { hardware_version_text_sensor_ = s; }
  void set_serial_number_text_sensor(text_sensor::TextSensor *s) { serial_number_text_sensor_ = s; }

  // Selects (write-back via Modbus function 0x06)
  void set_output_priority_select(select::Select *s) { output_priority_select_ = s; }
  void set_charge_priority_select(select::Select *s) { charge_priority_select_ = s; }

  // Numbers (write-back via Modbus function 0x06)
  void set_max_charge_current_number(number::Number *n) { max_charge_current_number_ = n; }
  void set_mains_charge_current_limit_number(number::Number *n) { mains_charge_current_limit_number_ = n; }
  void set_output_voltage_number(number::Number *n) { output_voltage_number_ = n; }

  // Switches (write-back via Modbus function 0x06; on=1, off=0)
  void set_eco_mode_switch(switch_::Switch *s) { eco_mode_switch_ = s; }

  // Exposed so the SrneSelect / SrneNumber subclasses can ask the hub to
  // write back a value when the user changes a control in HA.
  void write_register(uint16_t register_address, uint16_t value) {
    this->send_write_single(register_address, value);
  }

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

  // Block B storage
  sensor::Sensor *dc_bus_positive_voltage_sensor_{nullptr};
  sensor::Sensor *dc_bus_negative_voltage_sensor_{nullptr};
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

  // L2 storage
  sensor::Sensor *grid_voltage_l2_sensor_{nullptr};
  sensor::Sensor *inverter_voltage_l2_sensor_{nullptr};
  sensor::Sensor *inverter_current_l2_sensor_{nullptr};
  sensor::Sensor *load_current_l2_sensor_{nullptr};
  sensor::Sensor *load_active_power_l2_sensor_{nullptr};
  sensor::Sensor *load_apparent_power_l2_sensor_{nullptr};
  sensor::Sensor *load_percent_l2_sensor_{nullptr};

  // Binary sensors
  binary_sensor::BinarySensor *online_status_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *grid_present_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *inverter_on_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *fault_binary_sensor_{nullptr};

  // Text sensors
  text_sensor::TextSensor *machine_state_text_sensor_{nullptr};
  text_sensor::TextSensor *charge_state_text_sensor_{nullptr};
  text_sensor::TextSensor *fault_codes_text_sensor_{nullptr};
  text_sensor::TextSensor *software_version_text_sensor_{nullptr};
  text_sensor::TextSensor *hardware_version_text_sensor_{nullptr};
  text_sensor::TextSensor *serial_number_text_sensor_{nullptr};

  // Selects (writable, read back from settings block F1/F2)
  select::Select *output_priority_select_{nullptr};
  select::Select *charge_priority_select_{nullptr};

  // Numbers (writable, read back from settings block F1/F2)
  number::Number *max_charge_current_number_{nullptr};
  number::Number *mains_charge_current_limit_number_{nullptr};
  number::Number *output_voltage_number_{nullptr};

  // Switches (writable, read back from settings block F4)
  switch_::Switch *eco_mode_switch_{nullptr};

  uint8_t no_response_count_{0};
  uint32_t update_counter_{0};
  std::queue<uint8_t> expected_steps_;

  // One-shot register-space scan support
  bool scan_on_boot_{false};
  std::queue<uint16_t> scan_regs_in_flight_;
  uint32_t scan_total_{0};
  uint32_t scan_responded_{0};
  uint32_t scan_timed_out_{0};
  bool scan_complete_announced_{false};

  void queue_scan_();
  void log_scan_response_(uint16_t reg, const uint8_t *payload, size_t byte_count);

  void publish_state_(sensor::Sensor *s, float value);
  void publish_state_(binary_sensor::BinarySensor *s, bool state);
  void publish_state_(text_sensor::TextSensor *s, const std::string &state);
  void decode_block_a_(const uint8_t *payload, size_t byte_count);
  void decode_block_b0_(const uint8_t *payload, size_t byte_count);
  void decode_block_b1_(const uint8_t *payload, size_t byte_count);
  void decode_block_b2_(const uint8_t *payload, size_t byte_count);
  void decode_block_b3_(const uint8_t *payload, size_t byte_count);
  void decode_block_f4_(const uint8_t *payload, size_t byte_count);
  void decode_block_c_(const uint8_t *payload, size_t byte_count);
  void decode_block_d_(const uint8_t *payload, size_t byte_count);
  void decode_block_e_(const uint8_t *payload, size_t byte_count);
  void decode_block_f1_(const uint8_t *payload, size_t byte_count);
  void decode_block_f2_(const uint8_t *payload, size_t byte_count);
  std::string decode_machine_state_(uint16_t state);
  std::string decode_charge_state_(uint16_t state);
  std::string extract_low_byte_string_(const uint8_t *data, size_t length);
};

// Generic select wired to a single Modbus register. Options are indexed by
// position — option[0] writes raw 0, option[1] writes raw 1, etc. Matches the
// SRNE convention for output_priority / charge_priority / etc.
class SrneSelect : public select::Select, public Component {
 public:
  void set_parent(SrneInverter *parent) { parent_ = parent; }
  void set_register(uint16_t reg) { register_ = reg; }
  void control(const std::string &value) override;
  // Called by the parent decoder when a fresh raw value is read back.
  void publish_from_raw(uint16_t raw);

 protected:
  SrneInverter *parent_{nullptr};
  uint16_t register_{0};
};

// Generic number wired to a single Modbus register with a scale factor.
// scale=0.1 means HA value 14.4 → raw 144 on the wire (matches SRNE's
// "multiplier 10" voltage/current convention).
class SrneNumber : public number::Number, public Component {
 public:
  void set_parent(SrneInverter *parent) { parent_ = parent; }
  void set_register(uint16_t reg) { register_ = reg; }
  void set_scale(float scale) { scale_ = scale; }
  void control(float value) override;
  void publish_from_raw(uint16_t raw);

 protected:
  SrneInverter *parent_{nullptr};
  uint16_t register_{0};
  float scale_{1.0f};
};

// Generic switch wired to a single Modbus register. ON writes 1, OFF writes 0.
class SrneSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(SrneInverter *parent) { parent_ = parent; }
  void set_register(uint16_t reg) { register_ = reg; }
  void write_state(bool state) override;
  void publish_from_raw(uint16_t raw);

 protected:
  SrneInverter *parent_{nullptr};
  uint16_t register_{0};
};

}  // namespace srne_inverter
}  // namespace esphome
