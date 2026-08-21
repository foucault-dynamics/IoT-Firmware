#include "modbus_meter.h"

ModbusMeter::ModbusMeter() {
}

void ModbusMeter::init(ModbusRtuReader &reader, uint8_t slaveAddress, ModbusRegisterMap registers){
  this->reader = &reader;
  this->slaveAddress = slaveAddress;
  this->registers = registers;
}

float ModbusMeter::readVoltage(){
  return 0;
}

float ModbusMeter::readImportEnergy(){
  return 0;
}

float ModbusMeter::readExportEnergy(){
  return 0;
}
