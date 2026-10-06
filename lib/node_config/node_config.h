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

// What an IR node's optical port is actually wired to. Stored in the
// "simulate" NVS key, so the original 0/1 values keep their meaning.
enum class IrHeadMode : uint8_t {
  Real = 0,       // RealIrHead: the UART pins
  Simulated = 1,  // SimulatedIrHead: canned replies, no wires at all
  TcpSim = 2,     // TcpIrHead: IrSim/IrSimTCP.py over WiFi
};

// #### Transports ####

struct Rs485Config {
  uint8_t rx, tx, dere;
  uint32_t baudRate;
  SerialConfig format;
};

struct IrConfig {
  uint8_t rx, tx;
  uint32_t baudRate;
  SerialConfig format;
  // Flip RX and TX polarity in the UART. IEC 62056-21 sends a 0 bit as
  // light ON, but the IR circuit reads/drives light ON as HIGH, which a
  // plain UART treats as a 1.
  bool invert;
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
struct SoftApConfig {
  char ssid[33];
  char password[65];
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
  uint8_t channel;
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

struct Iec62056Config {
  uint32_t pollIntervalMs;

  IrConfig bus;
};

// #### Per-node configs ####

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

struct CvNodeConfig {
  uint8_t uid[16];
  // Where this node is installed. Set by upstream (NVS), not by hardware.
  uint8_t communityId;
  uint8_t unitId;
  SoftApConfig ap;
  EspNowConfig espNow;
  EspNowPeerConfig substation;
  CamHttpConfig cam;
};

struct IrNodeConfig {
  uint8_t uid[16];
  // Where this node is installed. Set by upstream (NVS), not by hardware.
  uint8_t communityId;
  uint8_t unitId;
  // Picks RealIrHead, SimulatedIrHead or TcpIrHead at runtime (the EE
  // team's UART-to-IR circuit doesn't exist yet).
  IrHeadMode headMode;
  SoftApConfig ap;  // TcpSim mode only
  EspNowConfig espNow;
  EspNowPeerConfig substation;
  Iec62056Config iec;
  TcpBusConfig tcp;  // TcpSim mode only
};

struct SubstationConfig {
  LoRaConfig lora;
  LoRaLinkConfig link;
  uint8_t espNowChannel;
};

struct GatewayConfig {
  LoRaConfig lora;
  LoRaLinkConfig link;
  WifiStationConfig wifi;
  MqttConfig mqtt;
};

#endif
