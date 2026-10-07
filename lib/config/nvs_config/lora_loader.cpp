/**
 * @file
 * LoRa radio and link loaders, shared by the substation and gateway loaders.
 */

#include "nvs_read.h"

#include "secrets.h"

namespace {

/**
 * LoRa SPI pins for the LilyGo TTGO LoRa32 v2.1 (classic ESP32).
 *
 * Board wiring, never an NVS key.
 */
const LoRaPins LORA_PINS = {/*sck*/ 5, /*miso*/ 19, /*mosi*/ 27, /*ss*/ 18, /*rst*/ 23, /*dio0*/ 26};

}  // namespace

LoRaConfig loadLoRaConfig() {
  LoRaConfig cfg{};
  cfg.band = readU32("lora_band", static_cast<uint32_t>(SECRET_LORA_BAND));
  cfg.spreadingFactor = static_cast<uint8_t>(readU32("lora_sf", 10));
  cfg.bandwidth = readU32("lora_bw", 125000);
  cfg.syncWord = static_cast<uint8_t>(readU32("lora_sync", 0xF3));
  cfg.txPower = static_cast<uint8_t>(readU32("lora_txpwr", 14));
  cfg.pins = LORA_PINS;
  return cfg;
}

LoRaLinkConfig loadLoRaLinkConfig() {
  LoRaLinkConfig cfg{};
  cfg.maxRetries = static_cast<uint8_t>(readU32("lora_retries", 3));
  cfg.ackTimeoutMs = readU32("lora_ack_ms", 1500);
  return cfg;
}
