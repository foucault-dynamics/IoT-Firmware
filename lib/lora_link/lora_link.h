#ifndef LORA_LINK_H
#define LORA_LINK_H

#include "loramodule.h"
#include "node_config.h"
#include "transmitter.h"

#include <cstddef>
#include <cstdint>

/*
 * Payload/ACK protocol and retries on top of a LoRaModule&. LoRa has no
 * delivery confirmation built into the radio the way ESP-NOW does, so this
 * layer supplies its own: sendPacket() retries until an AckPayload keyed on
 * the sent Payload's uid/seq comes back, or maxRetries is exhausted.
 *
 * address is ignored on both sides: LoRa is a single shared broadcast
 * channel here, not addressed like ESP-NOW.
 */
class LoRaLink : public Transmitter {
 private:
  LoRaModule &radio;
  LoRaLinkConfig config;

  // Reads exactly len bytes while radio.available(), so a short packet
  // fails the read instead of looping forever.
  bool readFrame(uint8_t *buf, size_t len);

 public:
  LoRaLink(LoRaModule &radio, const LoRaLinkConfig &config);

  int init() override;
  int sendPacket(const void *address, const uint8_t *buf, size_t len) override;
  int receivePacket(void *address, uint8_t *buf, size_t bufLen) override;
};

#endif
