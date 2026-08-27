#include "wifi_transmitter.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include <cstdlib>
#include <cstring>

namespace {
volatile bool sendComplete = false;
volatile bool lastSendOk = false;

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  (void)mac_addr;
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
  sendComplete = true;
}
}  // namespace

Wifi::Wifi(const EspNowConfig &config) : config_(config) {}

int Wifi::init() {
  WiFi.mode(config_.useApInterface ? WIFI_AP_STA : WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    return EXIT_FAILURE;
  }

  esp_now_register_send_cb(onDataSent);
  return EXIT_SUCCESS;
}

int Wifi::addPeer(const EspNowPeerConfig &peer) {
  esp_now_peer_info_t peerInfo{};
  memcpy(peerInfo.peer_addr, peer.mac, 6);
  peerInfo.channel = peer.channel;
  peerInfo.encrypt = false;
  peerInfo.ifidx = peer.useApInterface ? WIFI_IF_AP : WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    return EXIT_FAILURE;
  }

  memcpy(peerMac_, peer.mac, 6);
  peerAdded_ = true;
  return EXIT_SUCCESS;
}

int Wifi::send(const Payload &payload) {
  if (!peerAdded_) {
    return EXIT_FAILURE;
  }

  sendComplete = false;
  esp_err_t result = esp_now_send(peerMac_, reinterpret_cast<const uint8_t *>(&payload), sizeof(Payload));
  if (result != ESP_OK) {
    return EXIT_FAILURE;
  }

  unsigned long start = millis();
  while (!sendComplete && (millis() - start) < config_.sendTimeoutMs) {
    delay(1);
  }

  return (sendComplete && lastSendOk) ? EXIT_SUCCESS : EXIT_FAILURE;
}
