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
  char host[64];
  uint16_t port;
  uint32_t connectTimeoutMs;
};

// LoRa SPI pins. Board wiring, filled in by the loader from a constant, not
// read from NVS.
struct LoRaPins {
  uint8_t sck, miso, mosi, ss, rst, dio0;
};

struct LoRaConfig {
  uint32_t band;
  uint8_t spreadingFactor;
  uint32_t bandwidth;
  uint8_t syncWord;
  uint8_t txPower;
  LoRaPins pins;
};

// Retry/ACK behaviour for LoRaLink, shared by the substation and gateway.
struct LoRaLinkConfig {
  uint8_t maxRetries;
  uint32_t ackTimeoutMs;
};

// Access point the radio hosts.
struct WifiRadioConfig {
  char ssid[33];
  char password[65];
  uint8_t channel;
};

// Station credentials the gateway joins an existing network with.
struct WifiStationConfig {
  char ssid[33];
  char password[65];
};

struct MqttConfig {
  bool enabled;
  char server[64];
  uint16_t port;
  char topic[64];
  // Empty username connects without auth, same convention as HttpBusConfig.
  char username[32];
  char password[64];
};

// HTTP credentials and timeout used over the AP.
struct HttpBusConfig {
  uint32_t requestTimeoutMs;
  // Empty user disables HTTP basic auth.
  char httpUser[32];
  char httpPass[64];
};

struct EspNowConfig {
  uint32_t sendTimeoutMs;
};

struct EspNowPeerConfig {
  uint8_t mac[6];
};

// #### Protocol configs ####

//Format the Kwh values are stored
enum class RegisterFormat : uint8_t{
  ScaledInt = 0,
    IEEE_754Float = 1,
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

struct CamHttpConfig {
  uint32_t pollIntervalMs;

  HttpBusConfig bus;

  // AI-on-the-edge-device endpoint and the flow/ROI to read from it.
  char host[64];
  char path[32];
  char flowName[32];
};

// #### Per-node configs ####

struct Rs485NodeConfig {
  uint32_t uid;
  ReaderType readerType;
  WifiRadioConfig radio;
  EspNowConfig espNow;
  EspNowPeerConfig substation;
  ModbusRtuConfig modbus;
  TcpBusConfig tcp;
};

struct CvNodeConfig {
  uint32_t uid;
  WifiRadioConfig radio;
  EspNowConfig espNow;
  EspNowPeerConfig substation;
  CamHttpConfig cam;
};

struct SubstationConfig {
  LoRaConfig lora;
  LoRaLinkConfig link;
};

struct GatewayConfig {
  LoRaConfig lora;
  LoRaLinkConfig link;
  WifiStationConfig wifi;
  MqttConfig mqtt;
};

#endif
