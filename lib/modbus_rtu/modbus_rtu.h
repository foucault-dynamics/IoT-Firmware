#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "module.h"
#include "node_config.h"
#include "reader.h"
#include <cstdint>

// Char_len Parsing definitions
#define DATA_BITS_MASK 0b1100
#define PARITY_MASK 0b10
#define STOP_MASK 0b110000

// Packet specifications
#define REQUEST_LEN 8
#define RESPONSE_LEN 9
#define MIN_FRAME_LEN 4
#define TIMEOUT 500 // 500ms

class ModbusRtuReader: public Reader{
 public:
  // Deferred initialization of construction-time parameters.
  int init(Module &module, const void *config) override;
  int get_import(float *val) override;
  int get_export(float *val) override;
  int get_voltage(float *val) override;
private:
  const ModbusRtuConfig *config = nullptr;
  uint32_t t35_us = 0;
  uint32_t last_rx_us = 0;
  int read_register(uint16_t data_type_address, float *val);
  uint16_t modbus_crc(const uint8_t *data, size_t len);
  void build_request(uint8_t *buffer,uint16_t data_type_address);
  int read_response(uint8_t *buffer);
  void request_exception(uint8_t exception);
};

#endif
