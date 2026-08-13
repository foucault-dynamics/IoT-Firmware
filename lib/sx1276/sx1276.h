#ifndef SX1276_H
#define SX1276_H

#include "module.h"
#include "pin_config.h"

/*
 * SX1276 LoRa radio over SPI. Used as the uplink from a node to the
 * gateway, carrying Payload structs.
 *
 * Named Sx1276 rather than LoRa to avoid colliding with the
 * sandeepmistry LoRa library's global `LoRa` object.
 */
class Sx1276 : public Module {
 private:
  SpiPinConfig pinConfig;
  uint32_t band;
  uint32_t signalBandwidth;
  uint8_t spreadingFactor;
  uint8_t syncWord;
  uint8_t txPower;

 public:
  Sx1276(SpiPinConfig pins, uint32_t band, uint32_t bw, uint8_t sf,
         uint8_t sync, uint8_t pwr);
  int setup() override;  
  int send(const uint8_t *data, size_t len) override;
  int receive(uint8_t *buf, size_t maxLen) override;
  bool available() override;
};

#endif
