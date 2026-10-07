/**
 * @file
 * IR meter node: reads the meter's optical port with IEC 62056-21 mode C.
 *
 * An IrHead (RealIrHead or SimulatedIrHead) is the Module, and
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
#include "module.h"
#include "ir_config.h"
#include "nvs_config.h"
#include "nodes.h"
#include "reader.h"
#include "seq_counter.h"
#include "iec62056_21.h"
#include "esp_now_uplink.h"

static EspNowUplink *uplink = nullptr;  ///< Link to the substation.

static Module *bus = nullptr;     ///< IR head, real or simulated.
static Reader *reader = nullptr;  ///< Iec6205621Reader on top of #bus.

static IrNodeConfig cfg;  ///< Config loaded at setup.

static Payload payload;             ///< Reading sent each cycle. Identity fields are set once.
static unsigned long lastPoll = 0;  ///< millis() of the last reading attempt.
static bool readerReady = false;    ///< True once setup fully succeeded.

void irNodeSetup() {
  cfg = loadIrNodeConfig();

  uplink = new EspNowUplink(cfg.espNow);
  if (uplink->init() != EXIT_SUCCESS) {
    return;
  }

  if (uplink->addPeer(cfg.substation) != EXIT_SUCCESS) {
    return;
  }

  memcpy(payload.uid, cfg.uid, sizeof(payload.uid));
  payload.community_id = cfg.communityId;
  payload.unit_id = cfg.unitId;

  seqCounterBegin();

  // Real hardware doesn't exist yet (EE team's UART to IR circuit), so
  // this defaults to simulated. See loadIrNodeConfig()'s "simulate" key.
  bus = cfg.simulate
      ? static_cast<Module *>(new SimulatedIrHead())
      : static_cast<Module *>(new RealIrHead(cfg.iec.bus, Serial1));
  if (bus->init() != EXIT_SUCCESS) {
    Serial.println("[IR] IR head init failed.");
    return;
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

  if (uplink->sendPacket(cfg.substation.mac,
                         reinterpret_cast<const uint8_t *>(&payload),
                         sizeof(payload)) != EXIT_SUCCESS) {
    Serial.println("[IR] ESP-NOW send failed.");
  }
}
