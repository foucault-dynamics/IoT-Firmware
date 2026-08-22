#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <cstdint>
#include "HardwareSerial.h"

// #### Enumerators ####

// Which protocol a reader node speaks on its bus.
enum class ReaderType : uint8_t {
  ModbusRtu = 0,
    Iec62056 = 1,
    IEMS = 2
};
// Which meter a reader node is attached to. One entry per supported model;
enum class MeterModel : uint8_t {
  Simulated_Serial = 0,
    Simulated_Tcp = 1,
};

// #### Transports ####

struct Rs485Config {
  uint8_t rx, tx, dere;
  uint32_t baudRate;
  SerialConfig format;
};

// #### Protocol configs ####

struct ModbusRtuConfig {
  ReaderType reader;
  MeterModel meterModel;
  uint32_t pollIntervalMs;

  Rs485Config bus;

  uint8_t slaveAddress;
  uint8_t functionCode;
  uint16_t voltage_address, import_address, export_address;
};

// Hardcoded config should be resolved in runtime by upstream
ReaderType loadReaderType();
ModbusRtuConfig loadModbusRtuConfig();

#endif
