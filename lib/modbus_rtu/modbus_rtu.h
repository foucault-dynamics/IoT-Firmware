#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "module.h"
#include "node_config.h"
#include "reader.h"

#define DATA_BITS_MASK 0b1100
#define PARITY_MASK 0b10
#define STOP_MASK 0b110000

class ModbusRtuReader: public Reader{
 public:
  // Default constructor; call init() before use.
  ModbusRtuReader();
  // Deferred initialization of construction-time parameters.
  int init(Module &module, NodeConfig &config) override;
  float get_import() override;
  float get_export() override;
  float get_voltage() override;
private:  
  uint32_t t35_us;
  ModbusRtuConfig config;
  uint32_t last_rx_us = 0;
  
};

#endif
