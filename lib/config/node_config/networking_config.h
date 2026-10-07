#ifndef NETWORKING_CONFIG_H
#define NETWORKING_CONFIG_H

#include <cstdint>

// Access point the radio hosts.
struct SoftApConfig {
  char ssid[33];
  char password[65];
};

struct EspNowConfig {
  uint8_t channel;
  uint32_t sendTimeoutMs;
};

struct EspNowPeerConfig {
  uint8_t mac[6];
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

#endif