#include "real_ir_head.h"
#include <Arduino.h>

RealIrHead::RealIrHead(UartPinConfig pins, uint32_t initialBaud)
    : pinConfig(pins), baudRate(initialBaud) {}

int RealIrHead::setup() {
  Serial1.begin(baudRate, SERIAL_7E1, pinConfig.RX, pinConfig.TX);
  return 0;
}

int RealIrHead::send(const uint8_t *data, size_t len) {
  size_t written = Serial1.write(data, len);
  Serial1.flush();  // block until the bytes have actually left the wire
  return written;
}

int RealIrHead::receive(uint8_t *buf, size_t maxLen) {
  size_t n = 0;
  while (n < maxLen && Serial1.available()) {
    buf[n++] = Serial1.read();
  }
  return n;
}

bool RealIrHead::available() { return Serial1.available() > 0; }

void RealIrHead::setBaudRate(uint32_t baud) {
  baudRate = baud;
  Serial1.begin(baudRate, SERIAL_7E1, pinConfig.RX, pinConfig.TX);
}
