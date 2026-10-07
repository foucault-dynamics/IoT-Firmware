/**
 * @file
 * Config structs for the IR (optical port) node.
 */

#ifndef IR_CONFIG_H
#define IR_CONFIG_H

#include <cstdint>
#include "HardwareSerial.h"
#include "networking_config.h"

/** UART settings for the IR head. */
struct IrConfig {
  /** UART RX and TX GPIO numbers. */
  ///@{
  uint8_t rx, tx;
  ///@}
  uint32_t baudRate;    ///< Starting baud rate. IEC 62056-21 always opens at 300.
  SerialConfig format;  ///< Frame format. The standard fixes it at 7E1.
};

/** IEC 62056-21 reader settings. */
struct Iec62056Config {
  uint32_t pollIntervalMs;  ///< Time between readings, in ms.

  IrConfig bus;  ///< IR head UART settings.
};

/** Everything the IR node needs, filled in by loadIrNodeConfig(). */
struct IrNodeConfig {
  uint8_t uid[16];              ///< This board's eFuse UID.
  uint8_t communityId;          ///< Where the node is installed. Set from NVS, not hardware.
  uint8_t unitId;               ///< Unit within the community. Set from NVS, not hardware.
  /**
   * Picks SimulatedIrHead when true, RealIrHead when false.
   *
   * @todo Default to false once the EE team's UART to IR circuit exists.
   */
  bool simulate;
  EspNowConfig espNow;          ///< Uplink to the substation.
  EspNowPeerConfig substation;  ///< Substation to send readings to.
  Iec62056Config iec;           ///< Meter protocol settings.
};

#endif
