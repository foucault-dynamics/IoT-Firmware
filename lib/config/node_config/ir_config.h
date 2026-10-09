/**
 * @file
 * Config structs for the IR (optical port) node.
 */

#ifndef IR_CONFIG_H
#define IR_CONFIG_H

#include <cstdint>
#include "HardwareSerial.h"
#include "networking_config.h"
#include "rs485_config.h"  // TcpBusConfig, shared with the RS485 node's TcpBus

/**
 * What the IR node's optical port is wired to. Stored in the "simulate" NVS
 * key, so its original 0 and 1 values keep their meaning.
 */
enum class IrHeadMode : uint8_t {
  Real = 0,       ///< RealIrHead on the UART pins.
  Simulated = 1,  ///< SimulatedIrHead, canned replies with no wires at all.
  TcpSim = 2,     ///< TcpIrHead, reaching IrSim/IrSimTCP.py over WiFi.
};

/** UART settings for the IR head. */
struct IrConfig {
  /** UART RX and TX GPIO numbers. */
  ///@{
  uint8_t rx, tx;
  ///@}
  uint32_t baudRate;    ///< Starting baud rate. IEC 62056-21 always opens at 300.
  SerialConfig format;  ///< Frame format. The standard fixes it at 7E1.
  /**
   * Flips the UART's RX and TX polarity.
   *
   * IEC 62056-21 sends a 0 bit as light ON, but the IR circuit reads and drives
   * light ON as HIGH, which a plain UART treats as a 1.
   */
  bool invert;
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
   * Picks RealIrHead, SimulatedIrHead or TcpIrHead.
   *
   * @todo Default to IrHeadMode::Real once the EE team's UART to IR circuit
   *       exists.
   */
  IrHeadMode headMode;
  SoftApConfig ap;              ///< SoftAP the laptop joins. IrHeadMode::TcpSim only.
  EspNowConfig espNow;          ///< Uplink to the substation.
  EspNowPeerConfig substation;  ///< Substation to send readings to.
  Iec62056Config iec;           ///< Meter protocol settings.
  TcpBusConfig tcp;             ///< Where IrSimTCP.py runs. IrHeadMode::TcpSim only.
};

#endif
