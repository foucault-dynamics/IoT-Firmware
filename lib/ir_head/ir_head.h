#ifndef IR_HEAD_H
#define IR_HEAD_H

#include "module.h"

/*
 * Abstract base for anything that can stand in for the meter's IR optical
 * port: either the real UART-to-IR hardware (RealIrHead), or a software
 * stand-in that plays back canned meter responses (SimulatedIrHead).
 *
 * IEC 62056-21 mode C changes the link speed mid-session -- 300 baud for
 * the initial handshake, 19200 baud for the data block -- so this adds
 * setBaudRate() on top of the plain Module interface. That capability is
 * UART-specific, which is why it lives here rather than on Module itself
 * (an SPI-based module like the LoRa radio has no baud rate to change).
 */
class IrHead : public Module {
 public:
  virtual void setBaudRate(uint32_t baud) = 0;
};

#endif
