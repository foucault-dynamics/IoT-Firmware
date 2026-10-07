#include <Arduino.h>
#include <WiFi.h>
#include <cstdint>
#include <cstdlib>
#include "shared_payload.h"
#include "sp3485.h"
#include "tcp_bus.h"
#include "module.h"
#include "rs485_config.h"
#include "nvs_config.h"
#include "nodes.h"
#include "reader.h"
#include "seq_counter.h"
#include "modbus_rtu.h"
#include "wifi_radio.h"
#include "esp_now_uplink.h"

enum States {
  READ,
  SLEEP,
  SEND,
  REQUEST
};

// Substation details
static EspNowUplink *uplink;

// Meter Objects
static Module *bus;
static Reader *reader;

// Runtime configuration
static Rs485NodeConfig cfg;

// Time spent idling in SLEEP between meter reads
static constexpr unsigned long POLL_INTERVAL_MS = 60UL * 1000UL;

// State variables
static volatile States state = READ;
static unsigned long lastPoll = 0;
static bool readerReady = false;

static Payload payload;

void rs485NodeSetup() {

  cfg = loadRs485NodeConfig();

  // Start SoftAP if using the modbus over TCP
  if (cfg.readerType == ReaderType::ModbusTCP
      && !wifiRadioStartAp(cfg.ap, cfg.espNow.channel)) {
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

  // Store identity on payload
  memcpy(payload.uid,cfg.uid,sizeof(payload.uid));
  payload.community_id = cfg.communityId;
  payload.unit_id = cfg.unitId;

  seqCounterBegin();

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
    while(true){
      if(bus->init() == EXIT_SUCCESS) break;
      Serial.println("[RS485] TcpBus init failed.");      
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
    if(reader->get_import(&payload.kwh_import) == EXIT_FAILURE){
      return;
    }
    if(reader->get_export(&payload.kwh_export) == EXIT_FAILURE){
      return;
    }
    if(reader->get_voltage(&payload.voltage) == EXIT_FAILURE){
      return;
    }
    // Printing
    Serial.printf("import: %f\n",payload.kwh_import);
    Serial.printf("export: %f\n",payload.kwh_export);
    Serial.printf("voltage: %f\n",payload.voltage);    
    state = SEND;
    break;
  case SEND:
    //Sending
    payload.seq = seqNext();
    if(uplink->sendPacket(cfg.substation.mac,reinterpret_cast<const uint8_t *>(&payload),sizeof(payload)) == EXIT_FAILURE){
      Serial.printf("Error sending payload at sequence: %u\n",payload.seq);
      // return;
    }
    lastPoll = millis();
    state = SLEEP;
    break;
  case SLEEP:
    // Non blocking wait, loop() keeps running so other work is not starved
    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
      state = READ;
    }
    break;
  default:
    break;
  }
}
