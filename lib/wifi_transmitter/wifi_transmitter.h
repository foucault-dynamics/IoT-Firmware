#ifndef WIFI_TRANSMITTER_H
#define WIFI_TRANSMITTER_H

#include <cstdint>

#include "node_config.h"
#include "shared_payload.h"

/*
 * Thin wrapper around ESP-NOW: brings up the radio in station mode,
 * registers a single peer (the substation relay), and sends a Payload.
 *
 * Every node role (rs485, cv, ...) shares this so the ESP-NOW setup and
 * send/ack handling is written once.
 */
class Wifi {
 public:
  explicit Wifi(const EspNowConfig &config);

  // Brings up WiFi station mode and ESP-NOW. Returns EXIT_SUCCESS/EXIT_FAILURE.
  int init();

  // Registers an ESP-NOW peer. Returns EXIT_SUCCESS/EXIT_FAILURE.
  int addPeer(const EspNowPeerConfig &peer);

  // Sends a payload to the registered peer over ESP-NOW and blocks until the
  // send callback fires or sendTimeoutMs elapses.
  // Returns EXIT_SUCCESS/EXIT_FAILURE.
  int send(const Payload &payload);

 private:
  EspNowConfig config_;
  uint8_t peerMac_[6] = {0};
  bool peerAdded_ = false;
};

#endif
