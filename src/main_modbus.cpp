#include <Arduino.h>
#include "shared_payload.h"
#include "pin_config.h"
#include "sp3485.h"
#include "modbus_rtu.h"
#include "secrets.h"

// TODO: fill real pins/addresses once hardware is wired.
//
// Sp3485 is the Module: it owns the UART and the DE//RE line.
// ModbusRtuReader sits on top of it and owns framing, CRC16 and the
// register map.
//
// UartPinConfig rs485Pins = {/*RX*/ 0, /*TX*/ 1};
// Sp3485 bus(rs485Pins, 9600, /*DERE*/ 2);
// ModbusRtuReader meter(bus, /*slaveAddress*/ 1);

enum States{
  ESTABLISH_CONNECTION,
  READ,  
  SLEEP,  
}


UartPinConfig rs485Pins = {};
Sp3485 bus(rs485Pins,9600,2);
ModbusRtuReader meter(bus,1);

volatile States state;
const unsigned long POLL_INTERVAL = 1000; // ms

void setup() {
  // Setup bus
  bus.setup();
  // Setup meter
  meter.setup();
}

void loop() {
  // Payload p;
  // if (meter.poll(p) == 0) { /* push to uplink */ }
  switch(state){    
  case ESTABLISH_CONNECTION:
    if(meter.poll() == 0){
      Serial.println("Modbus poll failed");
      delay(1000);
      return;
    }
    break;
  case READ:
    if(meter.poll() == 0){
      Serial.println("Reading of data failed");
      delay(1000);
      return;
    }
    break;
  case SLEEP:
    break;
  default:
    break;
  }
  

  
}
