#include <Arduino.h>
#include <WiFi.h>
#include <cstdint>
#include <cstdlib>
#include "shared_payload.h"
#include "sp3485.h"
#include "tcp_bus.h"
#include "module.h"
#include "node_config.h"
#include "nvs_config.h"
#include "nodes.h"
#include "reader.h"
#include "modbus_rtu.h"
#include "wifi_radio.h"
#include "esp_now_uplink.h"

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

// Runtime configuration
static Rs485NodeConfig cfg;

// State variables
static volatile States state = READ;
static unsigned long lastPoll = 0;
static bool readerReady = false;

static Payload payload;

void rs485NodeSetup() {

  cfg = loadRs485NodeConfig();

  if (!wifiRadioStart(cfg.radio)) {
    Serial.println("[RS485] SoftAP bring-up failed, idling");
    readerReady = false;
    return;
  }

  // Initialise the ESP-NOW module
  uplink = new EspNowUplink(cfg.espNow);
  if (uplink->init() != EXIT_SUCCESS) {
    readerReady = false;
    return;
  }

  // Add substation as an ESP-NOW Peer
  if (uplink->addPeer(cfg.substation) != EXIT_SUCCESS) {
    readerReady = false;
    return;
  }

  switch (cfg.readerType) {
    // Modbus over Serial bus
  case ReaderType::ModbusRtu:{
    bus = new Sp3485(cfg.modbus.bus, Serial1);
    if (bus->init() != EXIT_SUCCESS) {
      Serial.println("[RS485] Sp3485 init failed.");
      break;
    }

    reader = new ModbusRtuReader(cfg.modbus);
    if (reader->init(*bus) == EXIT_SUCCESS) {
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
    bus = new TcpBus(cfg.tcp);
    if (bus->init() != EXIT_SUCCESS) {
      Serial.println("[RS485] TcpBus init failed.");
      break;
    }

    reader = new ModbusRtuReader(cfg.modbus);
    if (reader->init(*bus) == EXIT_SUCCESS) {
      readerReady = true;
    } else {
      delete reader;
      reader = nullptr;
    }
    break;
  }
  default:
    Serial.println("[RS485] Reader type not applicable to this node. Idling.");
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
    // Printing
    Serial.printf("import: %f\n",payload.kwh_import);
    Serial.printf("export: %f\n",payload.kwh_export);
    Serial.printf("voltage: %f\n",payload.voltage);


    uplink.
    
    //Sending
    delay(10000);
    break;

  case SLEEP:
    break;
  default:
    break;
  }
}
