/**
 * @file
 * LoRaLink implementation.
 */

#include "lora_link.h"

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "shared_payload.h"

LoRaLink::LoRaLink(LoRaModule &radio, const LoRaLinkConfig &config)
    : radio(radio), config(config), pending{}, attempt(0), phase(SendPhase::Idle), phaseStart(0) {
}

int LoRaLink::init() {
  return radio.init();
}

bool LoRaLink::readFrame(uint8_t *buf, size_t len) {
  size_t bytesRead = 0;
  while (bytesRead < len && radio.available()) {
    int value = radio.readByte();
    if (value >= 0) {
      buf[bytesRead++] = static_cast<uint8_t>(value);
    }
  }
  return bytesRead == len;
}

void LoRaLink::enterPhase(SendPhase next) {
  phase = next;
  phaseStart = millis();
}

int LoRaLink::beginSend(const uint8_t *buf, size_t len) {
  if (len != sizeof(Payload)) {
    Serial.println("[LoRaLink] Send: size mismatch");
    return EXIT_FAILURE;
  }
  if (phase != SendPhase::Idle) {
    Serial.println("[LoRaLink] Send already in flight");
    return EXIT_FAILURE;
  }

  memcpy(&pending, buf, sizeof(Payload));
  attempt = 0;
  enterPhase(SendPhase::Transmit);
  return EXIT_SUCCESS;
}

LoRaSendStatus LoRaLink::pollSend() {
  switch (phase) {
    case SendPhase::Idle:
      return LoRaSendStatus::Idle;

    case SendPhase::Transmit: {
      attempt++;
      char uidHex[UID_HEX_LEN];
      Serial.printf("[LoRa] Tx Attempt %u/%u | UID: %s | SEQ: %u\n", attempt, config.maxRetries, uidToHex(pending.uid, uidHex), pending.seq);

      if (radio.send(reinterpret_cast<const uint8_t *>(&pending), sizeof(Payload)) != EXIT_SUCCESS) {
        Serial.println("[LoRa] Failed to send packet");
        enterPhase(SendPhase::Backoff);
        return LoRaSendStatus::Pending;
      }

      radio.receive();
      enterPhase(SendPhase::AwaitAck);
      return LoRaSendStatus::Pending;
    }

    case SendPhase::AwaitAck: {
      int packetSize = radio.parsePacket();
      if (packetSize == static_cast<int>(sizeof(AckPayload))) {
        AckPayload ack{};
        if (readFrame(reinterpret_cast<uint8_t *>(&ack), sizeof(AckPayload)) && uidEquals(ack.uid, pending.uid) && ack.seq == pending.seq) {
          Serial.println("[LoRa] TX SUCCESS: ACK Received!");
          phase = SendPhase::Idle;
          return LoRaSendStatus::Acked;
        }
      } else if (packetSize > 0) {
        // Packet of any other size: drain it and keep waiting for the ACK.
        while (radio.available()) {
          radio.readByte();
        }
      }

      if (millis() - phaseStart >= config.ackTimeoutMs) {
        Serial.println("[LoRa] TX FAILED: ACK Timeout.");
        enterPhase(SendPhase::Backoff);
      }
      return LoRaSendStatus::Pending;
    }

    case SendPhase::Backoff:
      if (millis() - phaseStart < SEND_BACKOFF_MS) {
        return LoRaSendStatus::Pending;
      }
      if (attempt >= config.maxRetries) {
        Serial.println("[LoRa] TX CRITICAL: Max retries reached. Data dropped.");
        phase = SendPhase::Idle;
        return LoRaSendStatus::Failed;
      }
      enterPhase(SendPhase::Transmit);
      return LoRaSendStatus::Pending;
  }
  return LoRaSendStatus::Idle;
}

int LoRaLink::sendPacket(const void *address, const uint8_t *buf, size_t len) {
  (void)address; // Address is unused in Lora modules

  if (beginSend(buf, len) != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }

  LoRaSendStatus status;
  while ((status = pollSend()) == LoRaSendStatus::Pending) {
    // Let other tasks run between polls, as the old delay() backoff did
    delay(1);
  }
  return status == LoRaSendStatus::Acked ? EXIT_SUCCESS : EXIT_FAILURE;
}

int LoRaLink::receivePacket(void *address, uint8_t *buf, size_t bufLen) {
  (void)address;

  int packetSize = radio.parsePacket();
  if (packetSize <= 0) {
    return -1;
  }

  if (packetSize == sizeof(AckPayload)) {
    // An ACK broadcast meant for another node relaying at the same time.
    while (radio.available()) {
      radio.readByte();
    }
    return -1;
  }

  if (packetSize != static_cast<int>(sizeof(Payload))) {
    while (radio.available()) {
      radio.readByte();
    }
    Serial.printf("RX ERROR, Size Mismatch, Got: %dB", packetSize);
    return -1;
  }

  if (bufLen < sizeof(Payload)) {
    while (radio.available()) {
      radio.readByte();
    }
    Serial.println("[LoRaLink] Receive buffer too small, frame dropped");
    return -2;
  }

  if (!readFrame(buf, sizeof(Payload))) {
    return -1;
  }

  Payload received{};
  memcpy(&received, buf, sizeof(Payload));

  AckPayload ack{};
  memcpy(ack.uid, received.uid, sizeof(ack.uid));
  ack.seq = received.seq;

  // Give the sender time to switch its radio to receive before the ACK goes out
  delay(10);
  radio.send(reinterpret_cast<const uint8_t *>(&ack), sizeof(AckPayload));

  return static_cast<int>(sizeof(Payload));
}
