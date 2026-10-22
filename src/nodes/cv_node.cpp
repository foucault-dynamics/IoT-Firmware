/**
 * @file
 * CV (camera) meter node: reads the meter's display with an ESP32-CAM.
 *
 * Hosts its own Wi-Fi AP so the ESP32-CAM (AI-on-the-edge-device) can join it
 * directly, with no router or internet needed at the meter site. That matches
 * why the rest of this network uses ESP-NOW and LoRa instead of relying on
 * Wi-Fi. wifi_radio.h owns the AP, HttpBus is the Module on top of it, and
 * CamHttpReader reads the cam's "/json" API on top of that. Readings go to the
 * substation over ESP-NOW on the same radio, through its station interface
 * beside the AP.
 *
 * Setup runs the shared endNodeBringUp() checks: NVS, the AP, the HTTP bus
 * and ESP-NOW. The cam can take a minute or more to boot and join, so instead
 * of blocking setup on it, a MeterGate probes it every PROBE_RETRY_MS and only
 * declares the node ready, and starts reading, once it answers with every
 * configured flow. Reads that keep failing drop the node back to not ready.
 *
 * @todo Replace HttpBus with a wired UART link once the boards are physically
 *       connected. The reader above it stays unchanged.
 */

#include <Arduino.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "cam_http.h"
#include "http_bus.h"
#include "cv_config.h"
#include "nvs_config.h"
#include "nodes.h"
#include "reader.h"
#include "seq_counter.h"
#include "shared_payload.h"
#include "end_node_bringup.h"
#include "substation_link.h"

static SubstationLink *substation = nullptr;  ///< Link to the substation.

static HttpBus *bus = nullptr;              ///< HTTP client the cam is reached through.
static CamHttpReader *reader = nullptr;     ///< Reader on top of #bus.
static MeterGate *gate = nullptr;           ///< Holds off reading until the cam answers.

static CvNodeConfig cfg;  ///< Config loaded at setup.

static Payload payload;             ///< Reading sent each cycle. Identity fields are set once.
static unsigned long lastPoll = 0;  ///< millis() of the last reading attempt.
static bool setupOk = false;        ///< True once setup fully succeeded.

void cvNodeSetup() {
  if (loadCvNodeConfig(cfg) != EXIT_SUCCESS) {
    Serial.println("[CV] NVS unavailable, idling");
    return;
  }

  // The cam and the substation link share the radio: the cam joins the AP,
  // and ESP-NOW runs on the station interface beside it
  bus = new HttpBus(cfg.cam.bus);
  reader = new CamHttpReader(cfg.cam);
  EndNodeParts parts = {"CV", NodeType::Cv, cfg.uid, cfg.communityId, cfg.unitId,
                        cfg.espNow, &cfg.ap, bus, reader, BusPolicy::Required};

  if (endNodeBringUp(parts, substation, payload) != EXIT_SUCCESS) {
    return;
  }
  gate = new MeterGate("CV", *reader);
  setupOk = true;
}

void cvNodeLoop() {
  if (!setupOk) {
    return;
  }
  if (!gate->ready()) {
    // Read straight away once ready, instead of waiting out a whole interval
    lastPoll = millis() - cfg.cam.pollIntervalMs;
    return;
  }

  if ((millis() - lastPoll) < cfg.cam.pollIntervalMs) {
    return;
  }
  lastPoll = millis();

  float importVal = 0.0f;
  if (reader->get_import(&importVal) != EXIT_SUCCESS) {
    gate->reportRead(EXIT_FAILURE);
    return;
  }

  // Export only exists when the cam has a flow for it, otherwise it stays 0.
  // A failed export read drops the whole reading, so a stale value from the
  // last cycle is never sent beside a fresh import.
  float exportVal = payload.kwh_export;
  if (cfg.cam.exportFlowName[0] != '\0' && reader->get_export(&exportVal) != EXIT_SUCCESS) {
    gate->reportRead(EXIT_FAILURE);
    return;
  }
  gate->reportRead(EXIT_SUCCESS);
  payload.kwh_import = importVal;
  payload.kwh_export = exportVal;

  payload.seq = seqNext();

  Serial.printf("[CV] import=%.3f export=%.3f seq=%lu\n", payload.kwh_import, payload.kwh_export,
                static_cast<unsigned long>(payload.seq));

  if (substation->send(payload) != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW send failed.");
  }
}
