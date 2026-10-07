#ifndef RS485_CONFIG_H
#define RS485_CONFIG_H

#include <cstdint>
#include "HardwareSerial.h"
#include "networking_config.h"

// Which protocol a reader node speaks on its bus.
enum class ReaderType : uint8_t {
  ModbusRtu = 0,
    Iec62056 = 1,
    IEMS = 2,
    ModbusTCP = 3,
    CamHttp = 4
};
// Which meter a reader node is attached to. One entry per supported model;
enum class MeterModel : uint8_t {
  Simulated_Serial = 0,
    Simulated_Tcp = 1,
};

//Format the Kwh values are stored
enum class RegisterFormat : uint8_t{
  ScaledInt = 0,
    IEEE_754Float = 1,
};

struct Rs485Config {
  uint8_t rx, tx, dere;
  uint32_t baudRate;
  SerialConfig format;
};

struct TcpBusConfig {
  char host[64];
  uint16_t port;
  uint32_t connectTimeoutMs;
};

struct ModbusRtuConfig {
  MeterModel meterModel;
  uint32_t pollIntervalMs;

  Rs485Config bus;

  RegisterFormat registerFormat;
  uint8_t slaveAddress;
  uint8_t functionCode;
  uint16_t voltage_address, import_address, export_address;
};

struct Rs485NodeConfig {
  uint8_t uid[16];
  // Where this node is installed. Set by upstream (NVS), not by hardware.
  uint8_t communityId;
  uint8_t unitId;
  ReaderType readerType;
  SoftApConfig ap;
  EspNowConfig espNow;
  EspNowPeerConfig substation;
  ModbusRtuConfig modbus;
  TcpBusConfig tcp;
};

#endif