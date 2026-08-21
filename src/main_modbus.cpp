#include <Arduino.h>
#include "HardwareSerial.h"
#include "shared_payload.h"
#include "pin_config.h"
#include "sp3485.h"
#include "modbus_rtu.h"
#include "modbus_meter.h"
#include "secrets.h"

// TODO: fill real pins, slave address and register map once hardware is wired.

enum States{
  READ,
  SLEEP,
};

// Tunable settings


// Meter Objects
Sp3485 bus;
ModbusRtuReader reader;
ModbusMeter meter;

// State variables
volatile States state = READ;
const unsigned long POLL_INTERVAL = 1000; // ms
unsigned long lastPoll = 0;

Payload payload;

void setup() {
  //Setup First Serial module (for monitoring)
  Serial.begin(115200);

  UartPinConfig rs485Pins = {};
  SerialConfig config = SERIAL_8N1;
  
  // Setup SP3485 
  bus.init(rs485Pins, 9600, config, 2);
  bus.setup(); // Setup Serial controller

  // Setup Modbus
  reader.init(bus);

  /* BUILD IN FUTURE AN UPSTREAM REQUEST THAT
   *  WHAT METER WE ARE USING
   */
  uint8_t slave_address = 1;
  ModbusRegisterMap registerMap = {};

  // Initialise meter
  meter.init(reader, slave_address, registerMap);  
    
}

void loop() {
  switch(state){
  case READ:
    payload.voltage = meter.readVoltage();
    payload.kwh_import = meter.readImportEnergy();
    payload.kwh_export = meter.readExportEnergy();
    lastPoll = millis();
    state = SLEEP;
    break;
  case SLEEP:
    if(millis() - lastPoll >= POLL_INTERVAL){
      state = READ;
    }
    break;
  default:
    break;
  }
}
