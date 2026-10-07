/**
 * @file
 * Transmitter, the base class for anything that moves whole packets to a peer.
 */

#ifndef TRANSMITTER_H
#define TRANSMITTER_H

#include <cstddef>
#include <cstdint>

/**
 * Base class for a link that sends and receives whole packets.
 *
 * Unlike Module, which moves a byte stream over one bus, a Transmitter moves
 * complete frames to and from a peer (ESP-NOW, LoRa). The address is untyped
 * because each link addresses peers differently: ESP-NOW uses a 6 byte MAC,
 * LoRa is a shared broadcast channel and ignores it.
 */
class Transmitter{
 public:
  virtual ~Transmitter() = default;

  /**
   * Brings the link up so it is ready to send and receive.
   *
   * @retval EXIT_SUCCESS  Ready to use.
   * @retval EXIT_FAILURE  Setup failed, the link must not be used.
   */
  virtual int init() = 0;

  /**
   * Sends one packet to a peer and waits for delivery to be confirmed.
   *
   * @param[in] address  Peer address in the link's own format.
   * @param[in] buf      Packet bytes.
   * @param[in] len      Number of bytes in @p buf.
   * @retval EXIT_SUCCESS  The peer confirmed delivery.
   * @retval EXIT_FAILURE  The send failed or was never confirmed.
   */
  virtual int sendPacket(const void *address, const uint8_t *buf, size_t len) = 0;

  /**
   * Takes one received packet, without blocking.
   *
   * @param[out] address  Sender's address, or nullptr if not needed.
   * @param[out] buf      Destination for the packet bytes.
   * @param[in]  bufLen   Size of @p buf in bytes.
   * @return Number of bytes written to @p buf, -1 if no packet is waiting, or
   *         -2 if the packet did not fit in @p buf and was dropped.
   */
  virtual int receivePacket(void *address, uint8_t *buf, size_t bufLen) = 0;
};

#endif
