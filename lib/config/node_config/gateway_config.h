/**
 * @file
 * Config structs for the gateway node.
 */

#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#include <cstdint>
#include "networking_config.h"

/** Credentials the gateway uses to join an existing Wi-Fi network. */
struct WifiStationConfig {
  char ssid[33];      ///< Network name, up to 32 chars.
  char password[65];  ///< WPA2 passphrase.
};

/** MQTT broker the gateway publishes readings to. */
struct MqttConfig {
  bool enabled;        ///< False skips Wi-Fi and MQTT entirely (LoRa only testing).
  char server[64];     ///< Broker hostname.
  uint16_t port;       ///< Broker TLS port.
  char topic[64];      ///< Topic every reading is published on.
  char username[32];   ///< Empty connects without auth, same as HttpBusConfig.
  char password[64];   ///< Broker password.
};

/** Everything the gateway node needs, filled in by loadGatewayConfig(). */
struct GatewayConfig {
  LoRaConfig lora;          ///< Radio parameters.
  LoRaLinkConfig link;      ///< ACK and retry behaviour.
  WifiStationConfig wifi;   ///< Network to join for MQTT.
  MqttConfig mqtt;          ///< Broker to publish to.
};

#endif
