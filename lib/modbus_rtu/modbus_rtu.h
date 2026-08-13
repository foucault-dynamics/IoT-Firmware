#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "sp3485.h"
#include "shared_payload.h"
/*
 * Modbus RTU protocol read from the SP3485 transceiver
 */
class ModbusRtuReader {
 private:
  Sp3485 &stream;
  uint8_t slaveAddress;

 public:
  // Constructor
  ModbusRtuReader(Sp3485 &stream, uint8_t slaveAddress);
  // Setup
  int setup();
  // Receiving data
  int poll(Payload &out);
};

#endif
