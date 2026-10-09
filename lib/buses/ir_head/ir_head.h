/**
 * @file
 * IrHead, the base class for the meter's IR optical port.
 */

#ifndef IR_HEAD_H
#define IR_HEAD_H

#include "module.h"

/**
 * Base class for anything that can stand in for the meter's IR optical port.
 *
 * Either the real UART to IR hardware (RealIrHead), or a software stand in
 * that plays back canned meter responses (SimulatedIrHead).
 *
 * IEC 62056-21 mode C changes the link speed mid session: 300 baud for the
 * opening handshake, then whatever rate the meter offers for the data block.
 * So this adds setBaudRate() on top of the plain Module interface. That
 * capability is UART specific, which is why it lives here rather than on
 * Module itself, since an SPI module like the LoRa radio has no baud rate.
 */
class IrHead : public Module {
 public:
  /**
   * Switches the link to a new baud rate, keeping the frame format.
   *
   * @param[in] baud  New rate in baud.
   */
  virtual void setBaudRate(uint32_t baud) = 0;
};

#endif
