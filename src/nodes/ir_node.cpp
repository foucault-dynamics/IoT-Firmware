/**
 * @file
 * IR meter node: reads the meter's optical port with IEC 62056-21 mode C.
 *
 * An IrHead (RealIrHead, SimulatedIrHead or TcpIrHead) is the Module, and
 * Iec6205621Reader is the Reader on top of it. Setup runs the shared
 * endNodeBringUp() checks, then a MeterGate keeps the node not ready until the
 * meter answers its identification request. One handshake and data block read
 * yields a complete reading, so this follows cv_node.cpp's single poll per
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
#include "end_node_bringup.h"
#include "substation_link.h"

static SubstationLink *substation = nullptr;  ///< Link to the substation.

static Module *bus = nullptr;     ///< IR head, real or simulated.
static Reader *reader = nullptr;  ///< Iec6205621Reader on top of #bus.
static MeterGate *gate = nullptr; ///< Holds off reading until the meter answers.

static IrNodeConfig cfg;  ///< Config loaded at setup.

static Payload payload;             ///< Reading sent each cycle. Identity fields are set once.
static unsigned long lastPoll = 0;  ///< millis() of the last reading attempt.
static bool setupOk = false;        ///< True once setup fully succeeded.

void irNodeSetup() {
  if (loadIrNodeConfig(cfg) != EXIT_SUCCESS) {
    Serial.println("[IR] NVS unavailable, idling");
    return;
  }

  EndNodeParts parts = {"IR", NodeType::Ir, cfg.uid, cfg.communityId, cfg.unitId,
                        cfg.espNow, nullptr, nullptr, nullptr, BusPolicy::Required};

  // Real hardware doesn't exist yet (EE team's UART to IR circuit), so
  // this defaults to simulated. See loadIrNodeConfig()'s "simulate" key.
  switch (cfg.headMode) {
  case IrHeadMode::Real:
    bus = new RealIrHead(cfg.iec.bus, Serial1);
    break;
  case IrHeadMode::TcpSim:
    // The laptop running IrSimTCP.py joins this softAP, same as the RS485
    // node's ModbusTCP mode, and may not have yet
    bus = new TcpIrHead(cfg.tcp);
    parts.ap = &cfg.ap;
    parts.busPolicy = BusPolicy::Tolerated;
    break;
  case IrHeadMode::Simulated:
  default:
    bus = new SimulatedIrHead();
    break;
  }
  reader = new Iec6205621Reader(cfg.iec);
  parts.bus = bus;
  parts.reader = reader;

  if (endNodeBringUp(parts, substation, payload) != EXIT_SUCCESS) {
    return;
  }
  gate = new MeterGate("IR", *reader);
  setupOk = true;
}

void irNodeLoop() {
  if (!setupOk) {
    return;
  }
  if (!gate->ready()) {
    // Read straight away once ready, instead of waiting out a whole interval
    lastPoll = millis() - cfg.iec.pollIntervalMs;
    return;
  }

  if ((millis() - lastPoll) < cfg.iec.pollIntervalMs) {
    return;
  }
  lastPoll = millis();

  float importVal = 0.0f;
  int st = reader->get_import(&importVal);
  gate->reportRead(st);
  if (st != EXIT_SUCCESS) {
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
