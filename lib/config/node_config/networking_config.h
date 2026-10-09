/**
 * @file
 * Networking config structs shared by more than one node.
 *
 * A struct moves here from a node's own config header once a second node
 * uses it.
 */

#ifndef NETWORKING_CONFIG_H
#define NETWORKING_CONFIG_H

#include <cstdint>

/** Access point a node hosts on its own radio, for devices that join it. */
struct SoftApConfig {
  char ssid[33];      ///< Network name, up to 32 chars.
  char password[65];  ///< WPA2 passphrase, 8 to 64 chars.
};

/** ESP-NOW settings for a node's uplink to the substation. */
struct EspNowConfig {
  uint8_t channel;         ///< Wi-Fi channel, must match the substation's.
  uint32_t sendTimeoutMs;  ///< How long sendPacket() waits for delivery, in ms.
};

/** An ESP-NOW peer, identified by its MAC address. */
struct EspNowPeerConfig {
  uint8_t mac[6];  ///< Peer's station MAC address.
};

/**
 * LoRa SPI pins.
 *
 * Board wiring, filled in by the loader from a constant, never read from NVS.
 */
struct LoRaPins {
  /** GPIO numbers for the SPI bus, chip select, reset and the DIO0 interrupt. */
  ///@{
  uint8_t sck, miso, mosi, ss, rst, dio0;
  ///@}
};

/** Radio parameters for the SX1276. Both ends of a link must match. */
struct LoRaConfig {
  uint32_t band;            ///< Carrier frequency in Hz, e.g. 433E6 or 915E6.
  uint8_t spreadingFactor;  ///< 7 to 12. Higher reaches further but sends slower.
  uint32_t bandwidth;       ///< Signal bandwidth in Hz, e.g. 125000.
  uint8_t syncWord;         ///< Network ID byte. Radios ignore packets with a different one.
  uint8_t txPower;          ///< Transmit power in dBm.
  LoRaPins pins;            ///< Board wiring.
};

/** Retry and ACK behaviour for LoRaLink, shared by the substation and gateway. */
struct LoRaLinkConfig {
  uint8_t maxRetries;     ///< Send attempts before a packet is given up on.
  uint32_t ackTimeoutMs;  ///< How long each attempt waits for an ACK, in ms.
};

#endif
