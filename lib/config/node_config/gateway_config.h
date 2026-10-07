#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#include <cstdint>
#include "networking_config.h"

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

struct GatewayConfig {
  LoRaConfig lora;
  LoRaLinkConfig link;
  WifiStationConfig wifi;
  MqttConfig mqtt;
};

#endif