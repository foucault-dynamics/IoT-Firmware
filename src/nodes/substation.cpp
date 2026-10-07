#include <Arduino.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>

#include "esp_now_uplink.h"
#include "loramodule.h"
#include "lora_link.h"
#include "nodes.h"
#include "substation_config.h"
#include "nvs_config.h"
#include "reading_buffer.h"
#include "shared_payload.h"

// Substation node: receives Payloads from meter nodes over ESP-NOW and
// relays them over LoRa with an ACK/retry scheme, both owned by LoRaLink.

static LoRaModule *radio = nullptr;
static LoRaLink *loraLink = nullptr;
static EspNowUplink *uplink = nullptr;

static SubstationConfig cfg;

static constexpr unsigned long RETRY_BACKOFF_MS = 30UL * 1000UL;

static unsigned long nextSendAt = 0;
static bool ready = false;

void substationSetup() {
  cfg = loadSubstationConfig();

  radio = new LoRaModule(cfg.lora);
  loraLink = new LoRaLink(*radio, cfg.link);

  if (loraLink->init() != EXIT_SUCCESS) {
    Serial.println("[Substation] LoRa init failed, idling");
    return;
  }

  uplink = new EspNowUplink(EspNowConfig{cfg.espNowChannel, 0});
  if (uplink->init() != EXIT_SUCCESS) {
    Serial.println("[Substation] ESP-NOW init failed, idling");
    return;
  }

  Serial.println(">>> MAC Address: " + WiFi.macAddress() + " <<<");
  Serial.println("SUBSTATION: Ready to Relay");

  ready = true;
}

static void bufferIncoming() {
  Payload incoming;
  int len;
  while ((len = uplink->receivePacket(nullptr, reinterpret_cast<uint8_t *>(&incoming), sizeof(incoming))) != -1) {
    if (len != static_cast<int>(sizeof(Payload))) {
      Serial.println("[Error] Payload size mismatch!");
      continue;
    }
    char uidHex[UID_HEX_LEN];
    readingBufferPush(incoming);
    Serial.printf("[Substation] Buffered UID: %s | SEQ: %u | Total: %u\n", uidToHex(incoming.uid, uidHex), incoming.seq, readingBufferCount());
  }
}

void substationLoop() {
  if (!ready) {
    return;
  }

  bufferIncoming();

  if (static_cast<long>(millis() - nextSendAt) < 0) {
    return;
  }

  Payload pending;
  if (!readingBufferPeek(pending)) {
    return;
  }

  char uidHex[UID_HEX_LEN];
  Serial.printf("[Substation] Relaying UID: %s | SEQ: %u\n", uidToHex(pending.uid, uidHex), pending.seq);

  int result = loraLink->sendPacket(nullptr, reinterpret_cast<const uint8_t *>(&pending), sizeof(Payload));

  if (result == EXIT_SUCCESS) {
    readingBufferPop();
    Serial.printf("[Substation] Relay SUCCESS | UID: %s | SEQ: %u | Remaining: %u\n", uidToHex(pending.uid, uidHex), pending.seq, readingBufferCount());
  } else {
    nextSendAt = millis() + RETRY_BACKOFF_MS;
    Serial.printf("[Substation] Relay FAILED | UID: %s | SEQ: %u | Kept, retry in %lus | Buffered: %u\n", uidToHex(pending.uid, uidHex), pending.seq, RETRY_BACKOFF_MS / 1000, readingBufferCount());
  }
}
