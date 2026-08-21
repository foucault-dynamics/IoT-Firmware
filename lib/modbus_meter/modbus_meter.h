#ifndef MODBUS_METER_H
#define MODBUS_METER_H

#include "meter.h"
#include "modbus_rtu.h"
#include <cstdint>

struct ModbusRegisterMap {
  uint16_t voltage;
  uint16_t import_energy;
  uint16_t export_energy;
};

class ModbusMeter : public Meter {
 private:
  ModbusRtuReader *reader = nullptr;
  uint8_t slaveAddress;
  ModbusRegisterMap registers;

 public:
  // Default constructor; call init() before use.
  ModbusMeter();
  // Deferred initialization of construction-time parameters.
  void init(ModbusRtuReader &reader, uint8_t slaveAddress, ModbusRegisterMap registers);

  float readVoltage() override;
  float readImportEnergy() override;
  float readExportEnergy() override;
};

#endif
