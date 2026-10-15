/**
 * @file
 * CV (camera) meter node: reads the meter's display with an ESP32-CAM.
 *
 * The cam (AI-on-the-edge-device) is reached one of two ways, picked by the
 * `cam_link` NVS key (CvNodeConfig::camLink):
 *
 * - HTTP (0, the default): the node hosts its own Wi-Fi AP and the cam joins
 *   it directly, with no router or internet needed at the meter site.
 *   wifi_radio.h owns the AP and HttpBus is the Module on top of it.
 * - UART (1): a wire to the cam, through UartCamBus. No AP. A prototype, see
 *   docs/CV_WIRED_LINK.md.
 *
 * Either way CamHttpReader reads the cam's "/json" reply on top of the bus.
 * Readings go to the substation over ESP-NOW, through the radio's station
 * interface.
 *
 * Setup checks NVS, the AP and ESP-NOW. The cam can take a minute or more to
 * boot and join, so instead of blocking setup on it, the loop probes it every
 * CAM_PROBE_RETRY_MS and only declares the node ready, and starts reading,
 * once it answers with every configured flow.
 */

#include <Arduino.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "cam_http.h"
#include "http_bus.h"
#include "uart_cam_bus.h"
#include "cv_config.h"
#include "nvs_config.h"
#include "nodes.h"
#include "reader.h"
#include "seq_counter.h"
#include "shared_payload.h"
#include "wifi_radio.h"
#include "substation_link.h"

static SubstationLink *substation = nullptr;  ///< Link to the substation.

static Module *bus = nullptr;               ///< HttpBus or UartCamBus, picked by CvNodeConfig::camLink.
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

  if (cfg.camLink == CamLink::Uart) {
    Serial.println("[CV] Cam link: UART (wired)");
    // Serial is the C3's own USB, so Serial1 is free for the cam
    bus = new UartCamBus(cfg.camUart, Serial1);
  } else {
    Serial.println("[CV] Cam link: HTTP over the AP");
    // Bring up the radio the cam and the substation link both share
    if (!wifiRadioStartAp(cfg.ap, cfg.espNow.channel)) {
      Serial.println("[CV] Cannot continue without the AP up.");
      return;
    }
    bus = new HttpBus(cfg.cam.bus);
  }
  if (bus->init() != EXIT_SUCCESS) {
    Serial.println("[CV] Cam bus init failed.");
    return;
  }

  reader = new CamHttpReader(cfg.cam);
  if (reader->init(*bus) != EXIT_SUCCESS) {
    Serial.println("[CV] Cam HTTP reader init failed.");
    delete reader;
    reader = nullptr;
    return;
  }

  // ESP-NOW runs on the station interface, beside the AP in HTTP mode
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
    // Read straight away instead of waiting out a whole poll interval
    lastPoll = millis() - cfg.cam.pollIntervalMs;
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
