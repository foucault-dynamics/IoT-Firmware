#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include "secrets.h"
#include "shared_payload.h"
#include "sp3485.h"
#include "node_config.h"
#include "nodes.h"
#include "reader.h"
#include "modbus_rtu.h"
#include "wifi_transmitter.h"

enum States {
  READ,
  SLEEP,
  REQUEST
};

// Substation details
static Wifi *wifiLink;

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

  wifiLink = new Wifi(loadEspNowConfig());
  if (wifiLink->init() != EXIT_SUCCESS) {
    readerReady = false;
    return;
  }

  EspNowPeerConfig substation{};
  uint8_t substationMac[] = SECRET_MAC;
  memcpy(substation.mac, substationMac, 6);
  if (wifiLink->addPeer(substation) != EXIT_SUCCESS) {
    readerReady = false;
    return;
  }

  switch (loadReaderType()) {
  case ReaderType::ModbusRtu:{
    // Hardcoded for now
    cfg = loadModbusRtuConfig();

    // Setup SP3485
    bus = new Sp3485(cfg.bus, Serial1);
    bus->init();

    // Setup Mod bus
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
  case ReaderType::ModbusTCP:
    
    
    
    break;
  }

  state = READ;
  
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
    Serial.printf("import: %f\n",payload.kwh_import);
    Serial.printf("export: %f\n",payload.kwh_export);
    Serial.printf("voltage: %f\n",payload.voltage);
    delay(10000);
    break;
    
  case SLEEP:
    break;
  default:
    break;
  }
}
