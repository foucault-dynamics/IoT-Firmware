#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>

#include "loramodule.h"
#include "lora_link.h"
#include "nodes.h"
#include "node_config.h"
#include "nvs_config.h"
#include "shared_payload.h"

// Substation node: receives Payloads from meter nodes over ESP-NOW and
// relays them over LoRa with an ACK/retry scheme, both owned by LoRaLink.
// The ESP-NOW receive side stays a raw callback out of scope for this pass.

static LoRaModule *radio = nullptr;
static LoRaLink *loraLink = nullptr;

static SubstationConfig cfg;

static volatile bool hasNewDataToRelay = false;
static Payload pendingPayload;
static bool ready = false;

static void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len != sizeof(Payload)) {
    Serial.println("[Error] Payload size mismatch!");
    return;
  }
  memcpy(&pendingPayload, incomingData, sizeof(pendingPayload));
  hasNewDataToRelay = true;
}

void substationSetup() {
  cfg = loadSubstationConfig();

  radio = new LoRaModule(cfg.lora);
  loraLink = new LoRaLink(*radio, cfg.link);

  if (loraLink->init() != EXIT_SUCCESS) {
    Serial.println("[Substation] LoRa init failed, idling");
    return;
  }

  WiFi.mode(WIFI_STA);
  if (esp_wifi_set_channel(cfg.espNowChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.printf("[Substation] Failed to set channel %u, idling\n", cfg.espNowChannel);
    return;
  }
  if (esp_now_init() != ESP_OK) {
    Serial.println("[Substation] ESP-NOW init failed, idling");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println(">>> MAC Address: " + WiFi.macAddress() + " <<<");
  Serial.println("SUBSTATION: Ready to Relay");

  ready = true;
}

void substationLoop() {
  if (!ready || !hasNewDataToRelay) {
    return;
  }
  hasNewDataToRelay = false;

  char uidHex[UID_HEX_LEN];
  Serial.printf("[Substation] Relaying UID: %s | SEQ: %u\n", uidToHex(pendingPayload.uid, uidHex), pendingPayload.seq);

  int result = loraLink->sendPacket(nullptr, reinterpret_cast<const uint8_t *>(&pendingPayload), sizeof(Payload));

  if (result == EXIT_SUCCESS) {
    Serial.printf("[Substation] Relay SUCCESS | UID: %s | SEQ: %u | ACK received\n", uidToHex(pendingPayload.uid, uidHex), pendingPayload.seq);
  } else {
    Serial.printf("[Substation] Relay FAILED | UID: %s | SEQ: %u | Data dropped\n", uidToHex(pendingPayload.uid, uidHex), pendingPayload.seq);
  }
}
