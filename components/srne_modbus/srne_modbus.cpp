#include "srne_modbus.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace srne_modbus {

static const char *const TAG = "srne_modbus";

static const uint8_t MODBUS_READ_HOLDING_REGISTERS = 0x03;
static const uint8_t MODBUS_WRITE_SINGLE_REGISTER = 0x06;
static const uint8_t MODBUS_WRITE_MULTIPLE_REGISTERS = 0x10;

static const uint16_t SRNE_MODBUS_RESPONSE_TIMEOUT = 1000;
static const uint16_t SRNE_MODBUS_MIN_MSG_LEN = 5;
// Some SRNE firmware drops the next request if it arrives too soon after
// the previous response. Empirically a 50ms quiet period is enough.
static const uint16_t SRNE_MODBUS_QUIET_TIME = 50;

void SrneModbus::setup() {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
  }
}

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
    ESP_LOGW(TAG, "Modbus response timeout (partial frame) for 0x%02X fn 0x%02X reg 0x%04X cnt %u",
             this->in_flight_address_, this->in_flight_function_,
             this->in_flight_register_, this->in_flight_count_);
    this->rx_buffer_.clear();
    this->notify_timeout_();
    this->waiting_for_response_ = false;
  }

  if (this->waiting_for_response_ && this->rx_buffer_.empty() &&
      (now - this->last_send_ > SRNE_MODBUS_RESPONSE_TIMEOUT)) {
    ESP_LOGW(TAG, "No Modbus response from 0x%02X for reg 0x%04X (fn 0x%02X, cnt %u)",
             this->in_flight_address_, this->in_flight_register_,
             this->in_flight_function_, this->in_flight_count_);
    this->notify_timeout_();
    this->waiting_for_response_ = false;
  }

  if (!this->waiting_for_response_ && now >= this->quiet_until_) {
    this->send_next_request_();
  }
}

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

void SrneModbus::send_write_single(uint8_t address, uint16_t register_address, uint16_t value) {
  // Function 0x06 carries the value in the same two bytes that 0x03 uses for
  // num_registers, so we can reuse ModbusRequest by stuffing the value there.
  ModbusRequest request{address, MODBUS_WRITE_SINGLE_REGISTER, register_address, value, {}};
  this->request_queue_.push(request);
}

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
  this->in_flight_address_ = request.address;
  this->in_flight_function_ = request.function;
  this->in_flight_register_ = request.start_register;
  this->in_flight_count_ = request.num_registers;
}

void SrneModbus::notify_timeout_() {
  for (auto *device : this->devices_) {
    if (device->get_address() == this->in_flight_address_) {
      device->on_modbus_timeout();
    }
  }
}

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
  // Enforce a quiet period before the next request — some SRNE firmware
  // drops requests that follow a response too quickly.
  this->quiet_until_ = millis() + SRNE_MODBUS_QUIET_TIME;
  return true;
}

}  // namespace srne_modbus
}  // namespace esphome
