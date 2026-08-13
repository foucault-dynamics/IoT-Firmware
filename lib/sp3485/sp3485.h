#ifndef SP3485_H
#define SP3485_H

#include "uart.h"
#include <cstdint>

/*
 * SP3485 RS485 transceiver.
 *
 * Half duplex: the line must be released back to receive after every
 * transmission, and only after the last bit has physically left.
 *
 */
class Sp3485 : public Uart {
 private:
  uint8_t derePin;  // GPIO driving DE//RE. High = transmit, low = receive.

 public:
  // Constructor
  Sp3485(UartPinConfig pins, uint32_t baud, uint8_t dere);
  // Setup
  int setup() override;
  // Receive packets
  int receive(uint8_t *buf, size_t maxLen) override;
  // Send packets
  int send(const uint8_t *data, size_t len) override;  
};

#endif
