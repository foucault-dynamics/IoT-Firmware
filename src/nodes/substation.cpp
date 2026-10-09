/**
 * @file
 * Substation node: relays meter readings from ESP-NOW to LoRa.
 *
 * Readings arriving over ESP-NOW go into the reading buffer straight away.
 * One buffered reading at a time is then relayed over LoRa, whose ACK and
 * retry scheme lives in LoRaLink. A reading only leaves the buffer once the
 * gateway acknowledges it. After a failed relay the substation waits
 * RETRY_BACKOFF_MS before trying again, while still buffering new readings.
 */

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

static LoRaModule *radio = nullptr;     ///< LoRa radio.
static LoRaLink *loraLink = nullptr;    ///< ACK layer on top of #radio, toward the gateway.
static EspNowUplink *uplink = nullptr;  ///< ESP-NOW receiver for the meter nodes.

static SubstationConfig cfg;  ///< Config loaded at setup.

/** Wait after a failed relay before trying again, in ms. */
static constexpr unsigned long RETRY_BACKOFF_MS = 30UL * 1000UL;

static unsigned long nextSendAt = 0;  ///< millis() before which no relay is attempted.
static bool ready = false;            ///< True once setup fully succeeded.

void substationSetup() {
  cfg = loadSubstationConfig();

  radio = new LoRaModule(cfg.lora);
  loraLink = new LoRaLink(*radio, cfg.link);

  if (loraLink->init() != EXIT_SUCCESS) {
    Serial.println("[Substation] LoRa init failed, idling");
    return;
  }

  // Receive only, so the send timeout is never used
  uplink = new EspNowUplink(EspNowConfig{cfg.espNowChannel, 0});
  if (uplink->init() != EXIT_SUCCESS) {
    Serial.println("[Substation] ESP-NOW init failed, idling");
    return;
  }

  // Meter nodes need this MAC as their sub_mac setting
  Serial.println(">>> MAC Address: " + WiFi.macAddress() + " <<<");
  Serial.println("SUBSTATION: Ready to Relay");

  ready = true;
}

/** Moves every reading waiting in the ESP-NOW queue into the reading buffer. */
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

  // Signed difference so the check still works when millis() wraps
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
