/**
 * @file
 * Substation node: relays meter readings from ESP-NOW to LoRa.
 *
 * Readings arriving over ESP-NOW go into the reading buffer straight away.
 * One buffered reading at a time is then relayed over LoRa, whose ACK and
 * retry scheme lives in LoRaLink. A reading only leaves the buffer once the
 * gateway acknowledges it. After a failed relay the substation waits
 * RETRY_BACKOFF_MS before trying again, while still buffering new readings.
 *
 * A FreeRTOS task buffers ESP-NOW readings; the loop relays them.
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

static constexpr uint32_t ESPNOW_TASK_STACK = 4096;     ///< ESP-NOW task stack in bytes.
static constexpr UBaseType_t ESPNOW_TASK_PRIORITY = 3;  ///< ESP-NOW task priority.
static constexpr uint32_t ESPNOW_POLL_MS = 5;           ///< ESP-NOW poll interval in ms.

static SemaphoreHandle_t bufferLock = nullptr;  ///< Guards the reading buffer.
static unsigned long nextSendAt = 0;            ///< millis() before which no relay is attempted.
static bool ready = false;                      ///< True once setup fully succeeded.

/**
 * Moves every reading waiting in the ESP-NOW queue into the reading buffer.
 *
 * @param[in] arg  Unused.
 */
static void espNowRxTask(void *arg) {
  (void)arg;
  Payload incoming;
  int len;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(ESPNOW_POLL_MS));
    while ((len = uplink->receivePacket(nullptr, reinterpret_cast<uint8_t *>(&incoming), sizeof(incoming))) != -1) {
      if (len != static_cast<int>(sizeof(Payload))) {
        Serial.println("[Error] Payload size mismatch!");
        continue;
      }
      xSemaphoreTake(bufferLock, portMAX_DELAY);
      readingBufferPush(incoming);
      size_t total = readingBufferCount();
      xSemaphoreGive(bufferLock);
      char uidHex[UID_HEX_LEN];
      Serial.printf("[Substation] Buffered UID: %s | SEQ: %u | Total: %u\n", uidToHex(incoming.uid, uidHex), incoming.seq, total);
    }
  }
}

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

  bufferLock = xSemaphoreCreateMutex();
  if (bufferLock == nullptr) {
    Serial.println("[Substation] Mutex create failed, idling");
    return;
  }

  if (xTaskCreate(espNowRxTask, "espnow_rx", ESPNOW_TASK_STACK, nullptr, ESPNOW_TASK_PRIORITY, nullptr) != pdPASS) {
    Serial.println("[Substation] ESP-NOW task create failed, idling");
    return;
  }

  // Meter nodes need this MAC as their sub_mac setting
  Serial.println(">>> MAC Address: " + WiFi.macAddress() + " <<<");
  Serial.println("SUBSTATION: Ready to Relay");

  ready = true;
}

void substationLoop() {
  if (!ready) {
    return;
  }

  // Signed difference so the check still works when millis() wraps
  if (static_cast<long>(millis() - nextSendAt) < 0) {
    return;
  }

  Payload pending;
  xSemaphoreTake(bufferLock, portMAX_DELAY);
  bool havePending = readingBufferPeek(pending);
  xSemaphoreGive(bufferLock);
  if (!havePending) {
    return;
  }

  char uidHex[UID_HEX_LEN];
  Serial.printf("[Substation] Relaying UID: %s | SEQ: %u\n", uidToHex(pending.uid, uidHex), pending.seq);

  int result = loraLink->sendPacket(nullptr, reinterpret_cast<const uint8_t *>(&pending), sizeof(Payload));

  xSemaphoreTake(bufferLock, portMAX_DELAY);
  if (result == EXIT_SUCCESS) {
    readingBufferPop();
  }
  size_t remaining = readingBufferCount();
  xSemaphoreGive(bufferLock);

  if (result == EXIT_SUCCESS) {
    Serial.printf("[Substation] Relay SUCCESS | UID: %s | SEQ: %u | Remaining: %u\n", uidToHex(pending.uid, uidHex), pending.seq, remaining);
  } else {
    nextSendAt = millis() + RETRY_BACKOFF_MS;
    Serial.printf("[Substation] Relay FAILED | UID: %s | SEQ: %u | Kept, retry in %lus | Buffered: %u\n", uidToHex(pending.uid, uidHex), pending.seq, RETRY_BACKOFF_MS / 1000, remaining);
  }
}
