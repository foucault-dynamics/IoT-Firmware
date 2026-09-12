#include <Arduino.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "cam_http.h"
#include "http_bus.h"
#include "node_config.h"
#include "nodes.h"
#include "reader.h"
#include "secrets.h"
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

// Hardcoded for now, same as DEVICE_UID in main-esp-now-supermini.cpp.
static const uint32_t DEVICE_UID = 3;
static const uint8_t AP_CHANNEL = 1;
static const uint8_t SUBSTATION_MAC[] = SECRET_MAC;

// Substation details
static EspNowUplink *uplink = nullptr;

// Cam objects
static HttpBus *bus = nullptr;
static Reader *reader = nullptr;

// Runtime configuration. Hardcoded here rather than in node_config.cpp
// because the values come from secrets.h, which lives under src/.
static CamHttpConfig cfg;

// State variables
static Payload payload;
static uint32_t messageCounter = 0;
static unsigned long lastPoll = 0;
static bool readerReady = false;

static CamHttpConfig loadCamHttpConfig() {
  CamHttpConfig config{};
  config.reader = ReaderType::CamHttp;
  config.pollIntervalMs = 30000;

  config.bus.requestTimeoutMs = 5000;
  config.bus.httpUser = SECRET_CAM_USER;
  config.bus.httpPass = SECRET_CAM_PASS;

  config.host = SECRET_CAM_HOST;
  config.path = "/json";
  config.flowName = SECRET_CAM_FLOW_NAME;

  return config;
}

void cvNodeSetup() {
  // Hardcoded for now
  cfg = loadCamHttpConfig();

  // Bring up the radio the cam and the substation link both share
  WifiRadioConfig radioCfg{};
  radioCfg.ssid = SECRET_CAM_AP_SSID;
  radioCfg.password = SECRET_CAM_AP_PASSWORD;
  radioCfg.channel = AP_CHANNEL;
  if (!wifiRadioStart(radioCfg)) {
    Serial.println("[CV] Cannot continue without the AP up.");
    return;
  }

  // Setup the cam's HTTP client
  bus = new HttpBus(cfg.bus);
  bus->init();

  // Setup the cam's HTTP API
  reader = new CamHttpReader();
  if (reader->init(*bus, &cfg) != EXIT_SUCCESS) {
    Serial.println("[CV] Cam HTTP reader init failed.");
    delete reader;
    reader = nullptr;
    return;
  }

  // Setup ESP-NOW on the same AP radio
  EspNowConfig espNowCfg{};
  espNowCfg.sendTimeoutMs = 200;

  uplink = new EspNowUplink(espNowCfg);
  if (uplink->init() != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW init failed.");
    return;
  }

  EspNowPeerConfig substation{};
  memcpy(substation.mac, SUBSTATION_MAC, 6);
  if (uplink->addPeer(substation) != EXIT_SUCCESS) {
    Serial.println("[CV] Failed to add substation peer.");
    return;
  }

  payload.uid = DEVICE_UID;
  payload.community_id = 0;
  payload.unit_id = 0;

  readerReady = true;
}

void cvNodeLoop() {
  if (!readerReady) {
    return;
  }

  if ((millis() - lastPoll) < cfg.pollIntervalMs) {
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

  if (uplink->sendPacket(SUBSTATION_MAC, (const uint8_t *)&payload, sizeof(payload)) != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW send failed.");
  }
}
