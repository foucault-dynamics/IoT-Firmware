#include <Arduino.h>
#include <cstdlib>
#include "shared_payload.h"
#include "pin_config.h"
#include "sp3485.h"
#include "modbus_rtu.h"
#include "node_config.h"
#include "nodes.h"
#include "reader.h"

enum States {
  READ,
  SLEEP,
  REQUEST
};

// Meter Objects
static Sp3485 bus;
static Reader *reader;

// Runtime configuration (hardcoded in loadModbusRtuConfig() for now, requested
// from upstream later)
static ModbusRtuConfig cfg;

// State variables
static volatile States state = READ;
static unsigned long lastPoll = 0;
static bool readerReady = false;

static Payload payload;

void rs485NodeSetup() {

  switch (loadReaderType()) {
  case ReaderType::ModbusRtu:{
    // Hardcoded for now
    cfg = loadModbusRtuConfig();

    // Setup SP3485
    bus.init(
	     cfg.bus.rx,
	     cfg.bus.tx,
	     cfg.bus.dere,
	     cfg.bus.baudRate,
	     cfg.bus.format
	     );
    bus.setup();

    // Setup Modbus
    auto *modbusReader = new ModbusRtuReader();
    if (modbusReader->init(bus, cfg) == EXIT_SUCCESS){
      reader = modbusReader;
      readerReady = true;
    }
    break;
  }
  case ReaderType::IEMS:
    Serial.println("[RS485] IEMS reader not on this branch. Idling.");
    break;
  case ReaderType::Iec62056:
    Serial.println("Not applicable");
    break;
  }
}

void rs485NodeLoop() {

  if (!readerReady) {
    return;
  }
  
  switch (state) {
  case REQUEST:
    
    
  case READ:
    payload.kwh_import = reader->get_import();
    payload.kwh_export = reader->get_export();
    payload.voltage = reader->get_voltage();    
    break;
  case SLEEP:
    break;
  default:
    break;
  }
}
