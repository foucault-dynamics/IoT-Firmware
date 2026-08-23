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
#define TIMEOUT 1000

class ModbusRtuReader: public Reader{
 public:
  // Default constructor; call init() before use.
  ModbusRtuReader();
  // Deferred initialization of construction-time parameters.
  int init(Module &module, const ModbusRtuConfig &config);
  int get_import(float *val) override;
  int get_export(float *val) override;
  int get_voltage(float *val) override;
private:  
  uint32_t t35_us;
  ModbusRtuConfig config;
  uint32_t last_rx_us = 0;
  uint16_t modbus_crc(const uint8_t *data, size_t len);
  void build_request(uint8_t *buffer,uint32_t data_type_address);
  uint8_t request_exception(uint8_t exception);
};

#endif
