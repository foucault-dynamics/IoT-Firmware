/**
 * @file
 * TcpIrHead implementation.
 */

#include "tcp_ir_head.h"
#include <Arduino.h>

TcpIrHead::TcpIrHead(TcpBusConfig config) : bus(config) {}

int TcpIrHead::init() { return bus.init(); }

int TcpIrHead::send(const uint8_t *data, size_t len) { return bus.send(data, len); }

int TcpIrHead::readByte() { return bus.readByte(); }

bool TcpIrHead::available() { return bus.available(); }

void TcpIrHead::setBaudRate(uint32_t baud) {
  // No real link to reconfigure, same as SimulatedIrHead.
  Serial.printf("[TcpIrHead] would switch to %u baud\n", baud);
}
