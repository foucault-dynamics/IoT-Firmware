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
 * Setup checks NVS, the AP and ESP-NOW. The cam can take a minute or more to
 * boot and join, so instead of blocking setup on it, the loop probes it every
 * CAM_PROBE_RETRY_MS and only declares the node ready, and starts reading,
 * once it answers with every configured flow.
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
#include "wifi_radio.h"
#include "substation_link.h"

static SubstationLink *substation = nullptr;  ///< Link to the substation.

static HttpBus *bus = nullptr;              ///< HTTP client the cam is reached through.
static CamHttpReader *reader = nullptr;     ///< Reader on top of #bus.

static CvNodeConfig cfg;  ///< Config loaded at setup.

static Payload payload;             ///< Reading sent each cycle. Identity fields are set once.
static unsigned long lastPoll = 0;  ///< millis() of the last reading attempt.
static bool readerReady = false;    ///< True once setup fully succeeded.

/** Time between cam probes while waiting for it to answer, in ms. */
static constexpr unsigned long CAM_PROBE_RETRY_MS = 5000;

static bool camVerified = false;     ///< True once CamHttpReader::probe() passed. The node is ready.
static unsigned long lastProbe = 0;  ///< millis() of the last cam probe.

void cvNodeSetup() {
  if (loadCvNodeConfig(cfg) != EXIT_SUCCESS) {
    Serial.println("[CV] NVS unavailable, idling");
    return;
  }

  // Bring up the radio the cam and the substation link both share
  if (!wifiRadioStartAp(cfg.ap, cfg.espNow.channel)) {
    Serial.println("[CV] Cannot continue without the AP up.");
    return;
  }

  bus = new HttpBus(cfg.cam.bus);
  if (bus->init() != EXIT_SUCCESS) {
    Serial.println("[CV] HTTP bus init failed.");
    return;
  }

  reader = new CamHttpReader(cfg.cam);
  if (reader->init(*bus) != EXIT_SUCCESS) {
    Serial.println("[CV] Cam HTTP reader init failed.");
    delete reader;
    reader = nullptr;
    return;
  }

  // ESP-NOW runs on the station interface beside the AP, on the same channel
  substation = new SubstationLink(cfg.espNow, cfg.uid);
  if (substation->init() != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW init failed.");
    return;
  }

  // Not fatal: send() joins on the first reading if no substation answers now
  substation->join();

  memcpy(payload.uid, cfg.uid, sizeof(payload.uid));
  payload.community_id = cfg.communityId;
  payload.unit_id = cfg.unitId;

  if (seqCounterBegin() != EXIT_SUCCESS) {
    Serial.println("[CV] NVS unavailable for the sequence counter, idling");
    return;
  }

  readerReady = true;
}

void cvNodeLoop() {
  if (!readerReady) {
    return;
  }

  if (!camVerified) {
    if (lastProbe != 0 && (millis() - lastProbe) < CAM_PROBE_RETRY_MS) {
      return;
    }
    lastProbe = millis();
    if (reader->probe() != EXIT_SUCCESS) {
      return;
    }
    camVerified = true;
    Serial.println("[CV] Cam answered with every configured flow, node ready.");
  }

  if ((millis() - lastPoll) < cfg.cam.pollIntervalMs) {
    return;
  }
  lastPoll = millis();

  float importVal = 0.0f;
  if (reader->get_import(&importVal) != EXIT_SUCCESS) {
    return;
  }
  payload.kwh_import = importVal;

  // Export only exists when the cam has a flow for it, otherwise it stays 0.
  // A failed export read drops the whole reading, so a stale value from the
  // last cycle is never sent beside a fresh import.
  if (cfg.cam.exportFlowName[0] != '\0') {
    float exportVal = 0.0f;
    if (reader->get_export(&exportVal) != EXIT_SUCCESS) {
      return;
    }
    payload.kwh_export = exportVal;
  }

  payload.seq = seqNext();

  Serial.printf("[CV] import=%.3f export=%.3f seq=%lu\n", payload.kwh_import, payload.kwh_export,
                static_cast<unsigned long>(payload.seq));

  if (substation->send(payload) != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW send failed.");
  }
}
