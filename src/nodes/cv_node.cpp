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
#include "esp_now_uplink.h"

static EspNowUplink *uplink = nullptr;  ///< Link to the substation.

static HttpBus *bus = nullptr;      ///< HTTP client the cam is reached through.
static Reader *reader = nullptr;    ///< CamHttpReader on top of #bus.

static CvNodeConfig cfg;  ///< Config loaded at setup.

static Payload payload;             ///< Reading sent each cycle. Identity fields are set once.
static unsigned long lastPoll = 0;  ///< millis() of the last reading attempt.
static bool readerReady = false;    ///< True once setup fully succeeded.

void cvNodeSetup() {
  cfg = loadCvNodeConfig();

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
  uplink = new EspNowUplink(cfg.espNow);
  if (uplink->init() != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW init failed.");
    return;
  }

  if (uplink->addPeer(cfg.substation) != EXIT_SUCCESS) {
    Serial.println("[CV] Failed to add substation peer.");
    return;
  }

  memcpy(payload.uid, cfg.uid, sizeof(payload.uid));
  payload.community_id = cfg.communityId;
  payload.unit_id = cfg.unitId;

  seqCounterBegin();

  readerReady = true;
}

void cvNodeLoop() {
  if (!readerReady) {
    return;
  }

  if ((millis() - lastPoll) < cfg.cam.pollIntervalMs) {
    return;
  }
  lastPoll = millis();

  float reading = 0.0f;
  if (reader->get_import(&reading) != EXIT_SUCCESS) {
    return;
  }

  payload.kwh_import = reading;
  payload.seq = seqNext();

  Serial.printf("[CV] reading=%.3f seq=%lu\n", reading, static_cast<unsigned long>(payload.seq));

  if (uplink->sendPacket(cfg.substation.mac, (const uint8_t *)&payload, sizeof(payload)) != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW send failed.");
  }
}
