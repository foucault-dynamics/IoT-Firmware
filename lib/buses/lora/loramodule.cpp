#include "loramodule.h"

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

#include <cstdint>
#include <cstdlib>

LoRaModule::LoRaModule(const LoRaConfig &config) : config(config) {
}

int LoRaModule::init() {
  SPI.begin(config.pins.sck, config.pins.miso, config.pins.mosi, config.pins.ss);
  LoRa.setPins(config.pins.ss, config.pins.rst, config.pins.dio0);

  if (!LoRa.begin(config.band)) {
    Serial.println("[LoRaModule] LoRa.begin failed");
    return EXIT_FAILURE;
  }

  LoRa.setSignalBandwidth(config.bandwidth);
  LoRa.setSpreadingFactor(config.spreadingFactor);
  LoRa.setSyncWord(config.syncWord);
  LoRa.setTxPower(config.txPower);
  LoRa.enableCrc();

  return EXIT_SUCCESS;
}

int LoRaModule::send(const uint8_t *data, size_t len) {
  LoRa.beginPacket();
  LoRa.write(data, len);
  return LoRa.endPacket() != 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int LoRaModule::readByte() {
  if (LoRa.available()) {
    return LoRa.read();
  }
  return -1;
}

bool LoRaModule::available() {
  return LoRa.available() > 0;
}

int LoRaModule::parsePacket() {
  return LoRa.parsePacket();
}

void LoRaModule::receive() {
  LoRa.receive();
}

int LoRaModule::packetRssi() {
  return LoRa.packetRssi();
}

float LoRaModule::packetSnr() {
  return LoRa.packetSnr();
}
