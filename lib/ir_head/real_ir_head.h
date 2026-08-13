#ifndef REAL_IR_HEAD_H
#define REAL_IR_HEAD_H

#include "ir_head.h"
#include "pin_config.h"

/*
 * Real hardware: the EE team's UART-to-IR converter, wired to one of the
 * ESP32-C3's hardware UARTs (Serial1). From firmware's side this is plain
 * UART -- the IR modulation/demodulation happens entirely inside the EE
 * team's circuit, not here.
 *
 * Not usable until that circuit exists. Until then, build against
 * SimulatedIrHead instead (see main-esp-now-supermini.cpp).
 */
class RealIrHead : public IrHead {
 private:
  UartPinConfig pinConfig;
  uint32_t baudRate;

 public:
  RealIrHead(UartPinConfig pins, uint32_t initialBaud);

  int setup() override;
  int send(const uint8_t *data, size_t len) override;
  int receive(uint8_t *buf, size_t maxLen) override;
  bool available() override;
  void setBaudRate(uint32_t baud) override;
};

#endif
