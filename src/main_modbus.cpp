#include <Arduino.h>
#include "shared_payload.h"
#include "pin_config.h"
#include "sp3485.h"
#include "modbus_rtu.h"

// TODO: fill real pins/addresses once hardware is wired.
//
// Sp3485 is the Module: it owns the UART and the DE//RE line.
// ModbusRtuReader sits on top of it and owns framing, CRC16 and the
// register map.
//
// UartPinConfig rs485Pins = {/*RX*/ 0, /*TX*/ 1};
// Sp3485 bus(rs485Pins, 9600, /*DERE*/ 2);
// ModbusRtuReader meter(bus, /*slaveAddress*/ 1);

void setup() {
  // bus.setup();
  // meter.setup();
}

void loop() {
  // Payload p;
  // if (meter.poll(p) == 0) { /* push to uplink */ }
}
