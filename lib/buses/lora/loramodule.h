/**
 * @file
 * LoRaModule, the Module for the SX1276 LoRa radio.
 */

#ifndef LORA_MODULE_H
#define LORA_MODULE_H

#include "module.h"
#include "networking_config.h"

#include <cstddef>
#include <cstdint>

/**
 * SX1276 LoRa radio, driven through the sandeepmistry/LoRa library.
 *
 * Raw radio only: SPI and pins, radio parameters, one packet out, bytes in.
 * Framing, ACKs and retries live in LoRaLink, which holds a LoRaModule.
 *
 * parsePacket(), receive(), packetRssi() and packetSnr() live here rather than
 * in LoRaLink because the radio frames packets in hardware, so packet length
 * and signal quality come from the physical layer.
 */
class LoRaModule : public Module {
 private:
  LoRaConfig config;  ///< Pins and radio parameters.

 public:
  /**
   * Stores the config. The radio is not touched until init().
   *
   * @param[in] config  Pins and radio parameters, copied.
   */
  explicit LoRaModule(const LoRaConfig &config);

  /**
   * Starts SPI and the radio, then applies the radio parameters.
   *
   * Sets bandwidth, spreading factor, sync word and transmit power from the
   * config, and turns on the hardware CRC.
   *
   * @retval EXIT_SUCCESS  Radio found and configured.
   * @retval EXIT_FAILURE  LoRa.begin() failed, usually wiring or a wrong band.
   */
  int init() override;

  /**
   * Sends one packet and blocks until it has been transmitted.
   *
   * @param[in] data  Packet bytes.
   * @param[in] len   Number of bytes in @p data.
   * @retval EXIT_SUCCESS  Transmitted.
   * @retval EXIT_FAILURE  The radio reported a failure.
   */
  int send(const uint8_t *data, size_t len) override;

  /**
   * Reads the next byte of the packet found by parsePacket().
   *
   * @return The byte (0 to 255), or -1 if the packet is drained.
   */
  int readByte() override;

  /**
   * Reports whether bytes of the current packet are left, with no side effect.
   *
   * @retval true   readByte() has more bytes.
   * @retval false  The packet is drained, or none was parsed.
   */
  bool available() override;

  /**
   * Checks for a received packet and makes it the current one.
   *
   * @return Size of the packet in bytes, or 0 if none has arrived.
   */
  int parsePacket();

  /** Puts the radio into continuous receive mode, for waiting on an ACK. */
  void receive();

  /**
   * Signal strength of the last packet.
   *
   * @return RSSI in dBm.
   */
  int packetRssi();

  /**
   * Signal to noise ratio of the last packet.
   *
   * @return SNR in dB.
   */
  float packetSnr();
};

#endif
