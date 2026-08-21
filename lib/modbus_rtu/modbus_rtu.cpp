#include "modbus_rtu.h"

ModbusRtuReader::ModbusRtuReader() {
}

void ModbusRtuReader::init(Module &module){
  this->module = &module;
}

uint16_t ModbusRtuReader::readHoldingRegister(uint8_t slaveAddress, uint16_t address){
  return 0;
}

uint32_t ModbusRtuReader::readHoldingRegisters32(uint8_t slaveAddress, uint16_t address){
  return 0;
}
