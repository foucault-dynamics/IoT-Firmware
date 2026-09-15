#ifndef LORA_MODULE_H
#define LORA_MODULE_H

#include "module.h"
#include "node_config.h"

#include <cstddef>
#include <cstdint>

/*
 * SX1276 LoRa radio (sandeepmistry/LoRa). Raw radio only: SPI/pins, radio
 * parameters, one packet out, bytes in. Framing, ACK and retries live in
 * LoRaLink, which holds a LoRaModule&.
 */
class LoRaModule : public Module {
 private:
  LoRaConfig config;

 public:
  explicit LoRaModule(const LoRaConfig &config);

  // SPI + setPins from config.pins, then LoRa.begin(config.band). Returns
  // EXIT_FAILURE if LoRa.begin() fails, otherwise applies bandwidth,
  // spreading factor, sync word, tx power and CRC and returns EXIT_SUCCESS.
  int init() override;

  int send(const uint8_t *data, size_t len) override;

  int readByte() override;

  bool available() override;

  int parsePacket();
  void receive();

  int packetRssi();
  float packetSnr();
};

#endif
