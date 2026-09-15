#include "lora_link.h"

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "shared_payload.h"

LoRaLink::LoRaLink(LoRaModule &radio, const LoRaLinkConfig &config) : radio(radio), config(config) {
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

int LoRaLink::sendPacket(const void *address, const uint8_t *buf, size_t len) {
  (void)address;

  if (len != sizeof(Payload)) {
    Serial.println("[LoRaLink] sendPacket: size mismatch");
    return EXIT_FAILURE;
  }

  Payload sent{};
  memcpy(&sent, buf, sizeof(Payload));

  for (uint8_t attempt = 1; attempt <= config.maxRetries; attempt++) {
    Serial.printf("[LoRa] Tx Attempt %u/%u | UID: %u | SEQ: %u\n", attempt, config.maxRetries, sent.uid, sent.seq);

    if (radio.send(buf, len) != EXIT_SUCCESS) {
      Serial.println("[LoRa] Failed to send packet");
      delay(500);
      continue;
    }

    radio.receive();

    unsigned long startTime = millis();
    while (millis() - startTime < config.ackTimeoutMs) {
      int packetSize = radio.parsePacket();
      if (packetSize <= 0) {
        continue;
      }

      if (packetSize == sizeof(AckPayload)) {
        AckPayload ack{};
        if (readFrame(reinterpret_cast<uint8_t *>(&ack), sizeof(AckPayload)) && ack.uid == sent.uid && ack.seq == sent.seq) {
          Serial.println("[LoRa] TX SUCCESS: ACK Received!");
          return EXIT_SUCCESS;
        }
        continue;
      }

      // Packet of any other size: drain it and keep waiting for the ACK.
      while (radio.available()) {
        radio.readByte();
      }
    }

    Serial.println("[LoRa] TX FAILED: ACK Timeout.");
    delay(500);
  }

  Serial.println("[LoRa] TX CRITICAL: Max retries reached. Data dropped.");
  return EXIT_FAILURE;
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
  ack.uid = received.uid;
  ack.seq = received.seq;

  delay(10);
  radio.send(reinterpret_cast<const uint8_t *>(&ack), sizeof(AckPayload));

  return static_cast<int>(sizeof(Payload));
}
