#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/srne_modbus/srne_modbus.h"
#include <queue>

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

  std::queue<uint8_t> expected_steps_;

  void publish_state_(sensor::Sensor *s, float value);
  void decode_block_a_(const uint8_t *payload, size_t byte_count);
  void decode_block_b_(const uint8_t *payload, size_t byte_count);
};

}  // namespace srne_inverter
}  // namespace esphome
