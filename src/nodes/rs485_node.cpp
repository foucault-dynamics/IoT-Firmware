/**
 * @file
 * RS485 meter node: polls a Modbus RTU or DLMS/COSEM meter and sends readings to the
 * substation.
 *
 * The bus is an Sp3485 for a real meter, or a TcpBus for ModbusSim testing,
 * which also brings up a softAP for the simulator host to join. Setup runs the
 * shared endNodeBringUp() checks, then a MeterGate keeps the node not ready
 * until the meter answers a probe. Each cycle reads import, export and
 * voltage, sends them over ESP-NOW, then waits POLL_INTERVAL_MS without
 * blocking loop(). Reads that keep failing drop the node back to not ready.
 */

#include <Arduino.h>
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
#include "end_node_bringup.h"
#include "substation_link.h"

/** Steps of the node's read, send, sleep cycle. */
enum States {
  READ,    ///< Read import, export and voltage. Retries next loop on failure.
  SLEEP,   ///< Wait out POLL_INTERVAL_MS.
  SEND,    ///< Send the reading to the substation.
  REQUEST  ///< Unused.
};

static SubstationLink *substation;  ///< Link to the substation.

static Module *bus;     ///< Sp3485 or TcpBus, picked by Rs485NodeConfig::readerType.
static Reader *reader;  ///< ModbusRtuReader or DlmsCosemReader on top of #bus.
static MeterGate *gate; ///< Holds off reading until the meter answers.

static Rs485NodeConfig cfg;  ///< Config loaded at setup.

/** Time spent idling in SLEEP between meter reads, in ms. */
static constexpr unsigned long POLL_INTERVAL_MS = 60UL * 1000UL;

static volatile States state = READ;  ///< Current step of the cycle.
static unsigned long lastPoll = 0;    ///< millis() when the last send finished.
static bool setupOk = false;          ///< True once setup fully succeeded.

static Payload payload;  ///< Reading sent each cycle. Identity fields are set once.

void rs485NodeSetup() {

  if (loadRs485NodeConfig(cfg) != EXIT_SUCCESS) {
    Serial.println("[RS485] NVS unavailable, idling");
    return;
  }

  EndNodeParts parts = {"RS485", NodeType::Rs485, cfg.uid, cfg.communityId, cfg.unitId,
                        cfg.espNow, nullptr, nullptr, nullptr, BusPolicy::Required};

  switch (cfg.readerType) {
  case ReaderType::ModbusRtu:
    bus = new Sp3485(cfg.modbus.bus, Serial1);
    reader = new ModbusRtuReader(cfg.modbus);
    break;
  case ReaderType::DlmsCosem:
    bus = new Sp3485(cfg.dlms.bus, Serial1);
    reader = new DlmsCosemReader(cfg.dlms);
    break;
  case ReaderType::ModbusTCP:
    // Testing only. The simulator host joins this softAP, and may not have yet
    bus = new TcpBus(cfg.tcp);
    reader = new ModbusRtuReader(cfg.modbus);
    parts.ap = &cfg.ap;
    parts.busPolicy = BusPolicy::Tolerated;
    break;
  case ReaderType::DlmsTcp:
    bus = new TcpBus(cfg.tcp);
    reader = new DlmsCosemReader(cfg.dlms);
    parts.ap = &cfg.ap;
    parts.busPolicy = BusPolicy::Tolerated;
    break;
  case ReaderType::IEMS:
    Serial.println("[RS485] IEMS reader not on this branch. Idling.");
    return;
  case ReaderType::Iec62056:
    Serial.println("Not applicable");
    return;
  default:
    Serial.println("[RS485] Reader type not applicable to this node. Idling.");
    return;
  }
  parts.bus = bus;
  parts.reader = reader;

  if (endNodeBringUp(parts, substation, payload) != EXIT_SUCCESS) {
    return;
  }
  gate = new MeterGate("RS485", *reader);
  state = READ;
  setupOk = true;
}

void rs485NodeLoop() {

  if (!setupOk) {
    return;
  }
  if (!gate->ready()) {
    return;
  }

  switch (state) {
  case READ:
    if(reader->get_import(&payload.kwh_import) == EXIT_FAILURE){
      gate->reportRead(EXIT_FAILURE);
      return;
    }
    if(reader->get_export(&payload.kwh_export) == EXIT_FAILURE){
      gate->reportRead(EXIT_FAILURE);
      return;
    }
    if(reader->get_voltage(&payload.voltage) == EXIT_FAILURE){
      gate->reportRead(EXIT_FAILURE);
      return;
    }
    gate->reportRead(EXIT_SUCCESS);
    Serial.printf("import: %f\n",payload.kwh_import);
    Serial.printf("export: %f\n",payload.kwh_export);
    Serial.printf("voltage: %f\n",payload.voltage);
    state = SEND;
    break;
  case SEND:
    payload.seq = seqNext();
    if(substation->send(payload) == EXIT_FAILURE){
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
