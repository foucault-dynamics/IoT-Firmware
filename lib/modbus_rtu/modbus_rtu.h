#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "module.h"
#include <cstdint>

class ModbusRtuReader {
 private:
  Module *module = nullptr;

 public:
  // Default constructor; call init() before use.
  ModbusRtuReader();
  // Deferred initialization of construction-time parameters.
  void init(Module &module);

  uint16_t readHoldingRegister(uint8_t slaveAddress, uint16_t address);
  uint32_t readHoldingRegisters32(uint8_t slaveAddress, uint16_t address);
};

#endif
