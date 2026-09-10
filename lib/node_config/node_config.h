#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <cstdint>
#include "HardwareSerial.h"

// #### Enumerators ####

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

// #### Transports ####

struct Rs485Config {
  uint8_t rx, tx, dere;
  uint32_t baudRate;
  SerialConfig format;
};

struct TcpBusConfig {
  const char *host;
  uint16_t port;
  uint32_t connectTimeoutMs;
};

struct IrConfig{
};

struct LoRaConfig{
};

// Access point the ESP32-CAM joins, plus the HTTP credentials used on it.
struct CamWifiConfig {
  const char *ssid;
  const char *password;
  uint8_t channel;
  uint32_t requestTimeoutMs;
  // Empty user disables HTTP basic auth.
  const char *httpUser;
  const char *httpPass;
};

struct EspNowConfig {
  bool useApInterface;
  uint8_t channel;
  uint32_t sendTimeoutMs;
};

struct EspNowPeerConfig {
  uint8_t mac[6];
  uint8_t channel;
  bool useApInterface;
};

// #### Protocol configs ####

//Format the Kwh values are stored
enum class RegisterFormat : uint8_t{
  ScaledInt = 0,
    IEEE_754Float = 1,    
};

struct ModbusRtuConfig {
  ReaderType reader;
  MeterModel meterModel;
  uint32_t pollIntervalMs;

  Rs485Config bus;

  RegisterFormat registerFormat;
  uint8_t slaveAddress;
  uint8_t functionCode;
  uint16_t voltage_address, import_address, export_address;
};

struct CamHttpConfig {
  ReaderType reader;
  uint32_t pollIntervalMs;

  CamWifiConfig bus;

  // AI-on-the-edge-device endpoint and the flow/ROI to read from it.
  const char *host;
  const char *path;
  const char *flowName;
};

// Hardcoded config should be resolved in runtime by upstream
ReaderType loadReaderType();
ModbusRtuConfig loadModbusRtuConfig();
EspNowConfig loadEspNowConfig();
TcpBusConfig loadTcpBusConfig(const char *host, uint16_t port);

#endif
