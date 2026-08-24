#ifndef SP3485_H
#define SP3485_H

#include "module.h"
#include "node_config.h"
#include <cstdint>
#include "HardwareSerial.h"


/*
 * SP3485 RS485 transceiver.
 *
 * Half duplex: the line must be released back to receive after every
 * transmission, and only after the last bit has physically left.
 *
 */
class Sp3485 : public Module {
 private:
  uint8_t RX;
  uint8_t TX;  
  uint32_t baudRate;
  SerialConfig serialConfig;
  uint8_t derePin;  // GPIO driving DE//RE. High = transmit, low = receive.
  HardwareSerial *serial;

  // Blocks until the TX buffer has fully left the wire.
  void flush();
  void drainRX();

 public:
  // Default constructor; call init() before use.
  Sp3485(Rs485Config config, HardwareSerial &serial);
  // Deferred initialization of construction-time parameters.
  void init() override;
  // Receive packets
  int readByte() override;
  // Send packets
  int send(const uint8_t *data, size_t len) override;
  // True if at least one byte is waiting to be read.
  bool available() override;  
};

#endif
