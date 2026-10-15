/**
 * @file
 * IR meter node: reads the meter's optical port with IEC 62056-21 mode C.
 *
 * An IrHead (RealIrHead, SimulatedIrHead or TcpIrHead) is the Module, and
 * Iec6205621Reader is the Reader on top of it. One handshake and data block
 * read yields a complete reading, so this follows cv_node.cpp's single poll per
 * cycle shape rather than rs485_node.cpp's state machine.
 */

#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "shared_payload.h"
#include "real_ir_head.h"
#include "simulated_ir_head.h"
#include "tcp_ir_head.h"
#include "module.h"
#include "ir_config.h"
#include "nvs_config.h"
#include "nodes.h"
#include "reader.h"
#include "seq_counter.h"
#include "iec62056_21.h"
#include "substation_link.h"
#include "wifi_radio.h"

static SubstationLink *substation = nullptr;  ///< Link to the substation.

static Module *bus = nullptr;     ///< IR head, real or simulated.
static Reader *reader = nullptr;  ///< Iec6205621Reader on top of #bus.

static IrNodeConfig cfg;  ///< Config loaded at setup.

static Payload payload;             ///< Reading sent each cycle. Identity fields are set once.
static unsigned long lastPoll = 0;  ///< millis() of the last reading attempt.
static bool readerReady = false;    ///< True once setup fully succeeded.

void irNodeSetup() {
  if (loadIrNodeConfig(cfg) != EXIT_SUCCESS) {
    Serial.println("[IR] NVS unavailable, idling");
    return;
  }

  // TcpSim: the laptop running IrSimTCP.py joins this softAP, same as the
  // RS485 node's ModbusTCP mode.
  if (cfg.headMode == IrHeadMode::TcpSim
      && !wifiRadioStartAp(cfg.ap, cfg.espNow.channel)) {
    Serial.println("[IR] SoftAP bring-up failed, idling");
    return;
  }

  substation = new SubstationLink(cfg.espNow, cfg.uid);
  if (substation->init() != EXIT_SUCCESS) {
    return;
  }

  // Not fatal: send() joins on the first reading if no substation answers now
  substation->join();

  memcpy(payload.uid, cfg.uid, sizeof(payload.uid));
  payload.community_id = cfg.communityId;
  payload.unit_id = cfg.unitId;

  if (seqCounterBegin() != EXIT_SUCCESS) {
    Serial.println("[IR] NVS unavailable for the sequence counter, idling");
    return;
  }

  // Real hardware doesn't exist yet (EE team's UART to IR circuit), so
  // this defaults to simulated. See loadIrNodeConfig()'s "simulate" key.
  switch (cfg.headMode) {
  case IrHeadMode::Real:
    bus = new RealIrHead(cfg.iec.bus, Serial1);
    break;
  case IrHeadMode::TcpSim:
    bus = new TcpIrHead(cfg.tcp);
    break;
  case IrHeadMode::Simulated:
  default:
    bus = new SimulatedIrHead();
    break;
  }
  if (bus->init() != EXIT_SUCCESS) {
    if (cfg.headMode != IrHeadMode::TcpSim) {
      Serial.println("[IR] IR head init failed.");
      return;
    }
    // The laptop may not have joined the softAP yet. TcpBus reconnects on
    // every send(), so carry on and let the poll loop retry.
    Serial.println("[IR] IR sim not reachable yet, will retry each poll.");
  }

  reader = new Iec6205621Reader(cfg.iec);
  if (reader->init(*bus) == EXIT_SUCCESS) {
    readerReady = true;
  } else {
    delete reader;
    reader = nullptr;
  }
}

void irNodeLoop() {
  if (!readerReady) {
    return;
  }

  if ((millis() - lastPoll) < cfg.iec.pollIntervalMs) {
    return;
  }
  lastPoll = millis();

  float importVal = 0.0f;
  if (reader->get_import(&importVal) != EXIT_SUCCESS) {
    Serial.println("[IR] Meter read failed.");
    return;
  }
  payload.kwh_import = importVal;

  // Best effort: get_import() just read the whole data block, which
  // contains both OBIS codes, so this is free. Unlike CamHttpReader,
  // which genuinely has nothing to answer with for get_export().
  float exportVal = 0.0f;
  if (reader->get_export(&exportVal) == EXIT_SUCCESS) {
    payload.kwh_export = exportVal;
  }

  // get_voltage() always fails for this reader (no OBIS code for it), so
  // it is not called here, same as cv_node.cpp skips it for CamHttpReader.

  payload.seq = seqNext();

  Serial.printf("[IR] import=%.3f export=%.3f seq=%lu\n", payload.kwh_import,
                payload.kwh_export, static_cast<unsigned long>(payload.seq));

  if (substation->send(payload) != EXIT_SUCCESS) {
    Serial.println("[IR] ESP-NOW send failed.");
  }
}
