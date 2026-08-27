#include "real_ir_head.h"
#include <Arduino.h>
#include <cstdlib>

RealIrHead::RealIrHead(IrConfig config, HardwareSerial &serial)
    : RX(config.rx), TX(config.tx), baudRate(config.baudRate),
      serialConfig(config.format), serial(&serial) {}

void RealIrHead::init() {
  serial->begin(baudRate, serialConfig, RX, TX);
}

int RealIrHead::send(const uint8_t *data, size_t len) {
  size_t written = serial->write(data, len);
  serial->flush();  // block until the bytes have actually left the wire
  if (written != len) return EXIT_FAILURE;
  return EXIT_SUCCESS;
}

int RealIrHead::readByte() {
  if (available()) return serial->read();
  return -1;
}

bool RealIrHead::available() { return serial->available() > 0; }

void RealIrHead::setBaudRate(uint32_t baud) {
  baudRate = baud;
  serial->begin(baudRate, serialConfig, RX, TX);
}
