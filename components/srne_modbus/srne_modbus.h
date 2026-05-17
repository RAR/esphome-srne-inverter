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
  // Function 0x06: write a single 16-bit register.
  void send_write_single(uint8_t address, uint16_t register_address, uint16_t value);

 protected:
  bool parse_modbus_byte_(uint8_t byte);
  void send_next_request_();
  void notify_timeout_();

  GPIOPin *flow_control_pin_{nullptr};
  std::vector<uint8_t> rx_buffer_;
  uint32_t last_modbus_byte_{0};
  uint32_t last_send_{0};
  uint32_t quiet_until_{0};
  std::vector<SrneModbusDevice *> devices_;
  std::queue<ModbusRequest> request_queue_;
  bool waiting_for_response_{false};

  // Tracks the request currently on the wire, so we can tell the matching
  // device when it times out and log which register failed.
  uint8_t in_flight_address_{0};
  uint8_t in_flight_function_{0};
  uint16_t in_flight_register_{0};
  uint16_t in_flight_count_{0};
};

uint16_t crc16_modbus(const uint8_t *data, uint16_t len);

class SrneModbusDevice {
 public:
  void set_parent(SrneModbus *parent) { parent_ = parent; }
  void set_address(uint8_t address) { address_ = address; }
  uint8_t get_address() const { return address_; }

  virtual void on_modbus_data(const std::vector<uint8_t> &data) = 0;
  // Called by the hub when a request to this device's address times out.
  // Default no-op; devices that pipeline requests should override to drop
  // their per-request tracking so they stay in sync with the response stream.
  virtual void on_modbus_timeout() {}

  void send(uint8_t function, uint16_t start_register, uint16_t num_registers) {
    this->parent_->send(this->address_, function, start_register, num_registers);
  }
  void send_write_single(uint16_t register_address, uint16_t value) {
    this->parent_->send_write_single(this->address_, register_address, value);
  }

 protected:
  friend SrneModbus;
  SrneModbus *parent_;
  uint8_t address_;
};

}  // namespace srne_modbus
}  // namespace esphome
