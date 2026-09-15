#include <Arduino.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "cam_http.h"
#include "http_bus.h"
#include "node_config.h"
#include "nvs_config.h"
#include "nodes.h"
#include "reader.h"
#include "shared_payload.h"
#include "wifi_radio.h"
#include "esp_now_uplink.h"

// CV module node. Hosts its own WiFi access point so the ESP32-CAM
// (AI-on-the-edge-device) can join it directly with no router or internet
// needed at the meter site, matching why the rest of this network uses
// ESP-NOW/LoRa instead of relying on WiFi. The radio (lib/wifi_radio) owns
// the AP; the HTTP client on top of it is a Module (lib/http_bus); the cam's
// "/json" API and its reading are a Reader on top of that (lib/cam_http).
// Readings are forwarded to the substation over ESP-NOW on the same AP
// radio. A wired UART link (lib/esp32cam) is the planned replacement Module
// once the boards are physically connected; the reader above it stays
// unchanged.

// Substation details
static EspNowUplink *uplink = nullptr;

// Cam objects
static HttpBus *bus = nullptr;
static Reader *reader = nullptr;

// Runtime configuration
static CvNodeConfig cfg;

// State variables
static Payload payload;
static uint32_t messageCounter = 0;
static unsigned long lastPoll = 0;
static bool readerReady = false;

void cvNodeSetup() {
  cfg = loadCvNodeConfig();

  // Bring up the radio the cam and the substation link both share
  if (!wifiRadioStart(cfg.radio)) {
    Serial.println("[CV] Cannot continue without the AP up.");
    return;
  }

  // Setup the cam's HTTP client
  bus = new HttpBus(cfg.cam.bus);
  bus->init();

  // Setup the cam's HTTP API
  reader = new CamHttpReader(cfg.cam);
  if (reader->init(*bus) != EXIT_SUCCESS) {
    Serial.println("[CV] Cam HTTP reader init failed.");
    delete reader;
    reader = nullptr;
    return;
  }

  // Setup ESP-NOW on the same AP radio
  uplink = new EspNowUplink(cfg.espNow);
  if (uplink->init() != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW init failed.");
    return;
  }

  if (uplink->addPeer(cfg.substation) != EXIT_SUCCESS) {
    Serial.println("[CV] Failed to add substation peer.");
    return;
  }

  payload.uid = cfg.uid;
  payload.community_id = 0;
  payload.unit_id = 0;

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
  payload.seq = messageCounter++;

  Serial.printf("[CV] reading=%.3f seq=%lu\n", reading, static_cast<unsigned long>(payload.seq));

  if (uplink->sendPacket(cfg.substation.mac, (const uint8_t *)&payload, sizeof(payload)) != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW send failed.");
  }
}
