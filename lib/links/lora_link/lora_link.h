/**
 * @file
 * LoRaLink, the Transmitter that adds ACKs and retries on top of LoRaModule.
 */

#ifndef LORA_LINK_H
#define LORA_LINK_H

#include "loramodule.h"
#include "networking_config.h"
#include "transmitter.h"

#include <cstddef>
#include <cstdint>

/**
 * Payload and ACK protocol with retries, on top of a LoRaModule.
 *
 * LoRa has no delivery confirmation built into the radio the way ESP-NOW
 * does, so this layer supplies its own. sendPacket() retries until an
 * AckPayload keyed on the sent Payload's UID and sequence number comes back,
 * or LoRaLinkConfig::maxRetries is used up. receivePacket() replies to every
 * Payload with that ACK.
 *
 * The address is ignored on both sides, because LoRa is a single shared
 * broadcast channel here, not addressed like ESP-NOW.
 */
class LoRaLink : public Transmitter {
 private:
  LoRaModule &radio;      ///< Radio the link runs over.
  LoRaLinkConfig config;  ///< Retry count and ACK timeout.

  /**
   * Reads exactly @p len bytes of the current packet.
   *
   * Stops when the radio runs dry, so a short packet fails the read instead
   * of looping forever.
   *
   * @param[out] buf  Destination, at least @p len bytes.
   * @param[in]  len  Bytes to read.
   * @retval true   Read all @p len bytes.
   * @retval false  The packet ran out first.
   */
  bool readFrame(uint8_t *buf, size_t len);

 public:
  /**
   * Stores the radio and config. Nothing starts until init().
   *
   * @param[in] radio   Radio to run over. Must outlive the link.
   * @param[in] config  Retry count and ACK timeout, copied.
   */
  LoRaLink(LoRaModule &radio, const LoRaLinkConfig &config);

  /**
   * Starts the radio.
   *
   * @retval EXIT_SUCCESS  Radio ready.
   * @retval EXIT_FAILURE  LoRaModule::init() failed.
   */
  int init() override;

  /**
   * Sends a Payload, retrying until its ACK arrives.
   *
   * Each attempt sends the packet, switches to receive and waits up to
   * LoRaLinkConfig::ackTimeoutMs for a matching AckPayload. Packets of any
   * other size heard while waiting are discarded.
   *
   * @param[in] address  Ignored.
   * @param[in] buf      A Payload.
   * @param[in] len      Must be `sizeof(Payload)`.
   * @retval EXIT_SUCCESS  The matching ACK arrived.
   * @retval EXIT_FAILURE  Wrong size, or every attempt went unacknowledged.
   */
  int sendPacket(const void *address, const uint8_t *buf, size_t len) override;

  /**
   * Takes one received Payload and sends its ACK, without blocking.
   *
   * ACKs meant for other nodes and packets of the wrong size are discarded.
   *
   * @param[out] address  Ignored.
   * @param[out] buf      Destination for the Payload.
   * @param[in]  bufLen   Size of @p buf, at least `sizeof(Payload)`.
   * @return `sizeof(Payload)` on success, -1 if no usable Payload arrived, or
   *         -2 if @p buf is too small.
   */
  int receivePacket(void *address, uint8_t *buf, size_t bufLen) override;
};

#endif
