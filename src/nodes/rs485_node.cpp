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
static Sp3485 *bus;
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

    bus = new Sp3485(cfg.bus, Serial1);

    // Setup SP3485
    bus->init();

    // Setup Modbus
    reader = new ModbusRtuReader();
    if (reader->init(*bus, &cfg) == EXIT_SUCCESS) {
      readerReady = true;
    } else {
      delete reader;
      reader = nullptr;
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
  case READ:
    reader->get_import(&payload.kwh_import);
    reader->get_export(&payload.kwh_export);
    reader->get_voltage(&payload.voltage);    
    break;
  case SLEEP:
    break;
  default:
    break;
  }
}
