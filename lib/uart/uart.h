#ifndef UART_H
#define UART_H

#include "module.h"
#include "pin_config.h"

/*
 * Plain hardware UART (the ESP32-C3's own peripheral, Serial1).
 * Point-to-point, full duplex, no direction control.
 *
 * Used directly for the link to the ESP32-CAM board. For the RS485 bus
 * see Sp3485, which extends this with half-duplex direction control.
 */
class Uart : public Module {
 protected:
  UartPinConfig pinConfig;
  uint32_t baudRate;

  // Blocks until the TX buffer has fully left the wire.
  void flush();

 public:
  Uart(UartPinConfig pins, uint32_t baud);
  int setup() override;
  int send(const uint8_t *data, size_t len) override;
  int receive(uint8_t *buf, size_t maxLen) override;
  bool available() override;
};

#endif
