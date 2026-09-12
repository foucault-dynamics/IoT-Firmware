#include <Arduino.h>
#include <WiFi.h>
#include <cstdint>
#include <cstdlib>
#include "secrets.h"
#include "shared_payload.h"
#include "sp3485.h"
#include "tcp_bus.h"
#include "module.h"
#include "node_config.h"
#include "nodes.h"
#include "reader.h"
#include "modbus_rtu.h"
#include "wifi_radio.h"
#include "esp_now_uplink.h"

#define SOFTAP_CHANNEL 6   // must be 2.4 GHz, 1-11; C3 has no 5 GHz radio

enum States {
  READ,
  SLEEP,
  REQUEST
};

// Substation details
static EspNowUplink *uplink;

// Meter Objects
static Module *bus;
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

  // Hardcoded to take in ModBusTCP
  ReaderType readerType = loadReaderType();

  WifiRadioConfig radioCfg{};
  radioCfg.ssid = SECRET_AP_SSID;
  radioCfg.password = SECRET_AP_PASS;
  radioCfg.channel = SOFTAP_CHANNEL;
  if (!wifiRadioStart(radioCfg)) {
    Serial.println("[RS485] SoftAP bring-up failed, idling");
    readerReady = false;
    return;
  }

  // Initialise the ESP-NOW module
  uplink = new EspNowUplink(loadEspNowConfig());
  if (uplink->init() != EXIT_SUCCESS) {
    readerReady = false;
    return;
  }


  // Add substation as an ESP-NOW Peer
  EspNowPeerConfig substation{};
  uint8_t substationMac[] = SECRET_MAC;
  memcpy(substation.mac, substationMac, 6);
  if (uplink->addPeer(substation) != EXIT_SUCCESS) {
    readerReady = false;
    return;
  }

  switch (readerType) {
    // Modbus over Serial bus
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
    // Modbus over TCP (only for testing)
  case ReaderType::ModbusTCP: {
    cfg = loadModbusRtuConfig();
   
    bus = new TcpBus(loadTcpBusConfig(SECRET_MODBUS_SIM_HOST, SECRET_MODBUS_SIM_PORT));
    bus->init();

    reader = new ModbusRtuReader();
    if (reader->init(*bus, &cfg) == EXIT_SUCCESS) {
      readerReady = true;
    } else {
      delete reader;
      reader = nullptr;
    }
    break;
  }
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
