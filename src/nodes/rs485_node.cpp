/**
 * @file
 * RS485 meter node: polls a Modbus RTU or DLMS/COSEM meter and sends readings to the
 * substation.
 *
 * The bus is an Sp3485 for a real meter, or a TcpBus for ModbusSim testing,
 * which also brings up a softAP for the simulator host to join. Each cycle
 * reads import, export and voltage, sends them over ESP-NOW, then waits
 * POLL_INTERVAL_MS without blocking loop().
 */

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
#include "dlms_cosem.h"
#include "wifi_radio.h"
#include "esp_now_uplink.h"

/** Steps of the node's read, send, sleep cycle. */
enum States {
  READ,    ///< Read import, export and voltage. Retries next loop on failure.
  SLEEP,   ///< Wait out POLL_INTERVAL_MS.
  SEND,    ///< Send the reading to the substation.
  REQUEST  ///< Unused.
};

static EspNowUplink *uplink;  ///< Link to the substation.

static Module *bus;     ///< Sp3485 or TcpBus, picked by Rs485NodeConfig::readerType.
static Reader *reader;  ///< ModbusRtuReader or DlmsCosemReader on top of #bus.

static Rs485NodeConfig cfg;  ///< Config loaded at setup.

/** Time spent idling in SLEEP between meter reads, in ms. */
static constexpr unsigned long POLL_INTERVAL_MS = 60UL * 1000UL;

static volatile States state = READ;  ///< Current step of the cycle.
static unsigned long lastPoll = 0;    ///< millis() when the last send finished.
static bool readerReady = false;      ///< True once setup fully succeeded.

static Payload payload;  ///< Reading sent each cycle. Identity fields are set once.

void rs485NodeSetup() {

  cfg = loadRs485NodeConfig();

  // The simulator host joins this softAP to reach the TCP bus
  if (cfg.readerType == ReaderType::ModbusTCP
      && !wifiRadioStartAp(cfg.ap, cfg.espNow.channel)) {
    Serial.println("[RS485] SoftAP bring-up failed, idling");
    readerReady = false;
    return;
  }

  uplink = new EspNowUplink(cfg.espNow);
  if (uplink->init() != EXIT_SUCCESS) {
    readerReady = false;
    return;
  }

  if (uplink->addPeer(cfg.substation) != EXIT_SUCCESS) {
    readerReady = false;
    return;
  }

  memcpy(payload.uid,cfg.uid,sizeof(payload.uid));
  payload.community_id = cfg.communityId;
  payload.unit_id = cfg.unitId;

  seqCounterBegin();

  switch (cfg.readerType) {
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
  case ReaderType::DlmsCosem:{
    bus = new Sp3485(cfg.dlms.bus, Serial1);
    if (bus->init() != EXIT_SUCCESS) {
      Serial.println("[RS485] Sp3485 init failed.");
      break;
    }

    reader = new DlmsCosemReader(cfg.dlms);
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
  case ReaderType::ModbusTCP: {
    bus = new TcpBus(cfg.tcp);
    // Testing only, so keep retrying until the simulator is reachable
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
    Serial.printf("import: %f\n",payload.kwh_import);
    Serial.printf("export: %f\n",payload.kwh_export);
    Serial.printf("voltage: %f\n",payload.voltage);
    state = SEND;
    break;
  case SEND:
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
