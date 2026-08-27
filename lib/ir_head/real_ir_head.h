#ifndef REAL_IR_HEAD_H
#define REAL_IR_HEAD_H

#include "ir_head.h"
#include "node_config.h"
#include "HardwareSerial.h"

/*
 * Real hardware: the EE team's UART-to-IR converter, wired to one of the
 * ESP32-C3's hardware UARTs. From firmware's side this is plain UART --
 * the IR modulation/demodulation happens entirely inside the EE team's
 * circuit, not here. Which physical UART it's wired to (Serial1, etc.) is
 * up to the caller, passed in as `serial` -- same pattern Sp3485 uses.
 *
 * Not usable until that circuit exists. Until then, build against
 * SimulatedIrHead instead (see main-esp-now-supermini.cpp).
 */
class RealIrHead : public IrHead {
 private:
  uint8_t RX;
  uint8_t TX;
  uint32_t baudRate;
  SerialConfig serialConfig;
  HardwareSerial *serial;

 public:
  RealIrHead(IrConfig config, HardwareSerial &serial);

  void init() override;
  int send(const uint8_t *data, size_t len) override;
  int readByte() override;
  bool available() override;
  void setBaudRate(uint32_t baud) override;
};

#endif
