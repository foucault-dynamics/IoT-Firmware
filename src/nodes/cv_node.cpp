#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>

#include "nodes.h"
#include "node_config.h"
#include "secrets.h"
#include "shared_payload.h"
#include "wifi_transmitter.h"

// CV module node. Hosts its own WiFi access point so the ESP32-CAM
// (AI-on-the-edge-device) can join it directly with no router or internet
// needed at the meter site, matching why the rest of this network uses
// ESP-NOW/LoRa instead of relying on WiFi. Polls the cam's HTTP "/json" API
// for the current meter reading and forwards it to the substation over
// ESP-NOW on the same AP radio. A wired UART link (lib/esp32cam) is the
// planned replacement once the boards are physically connected.

// Hardcoded for now, same as DEVICE_UID in main-esp-now-supermini.cpp.
static const uint32_t DEVICE_UID = 3;
static const uint32_t POLL_INTERVAL_MS = 30000;
static const uint8_t AP_CHANNEL = 1;

namespace {
Wifi *wifiLink = nullptr;
Payload payload{};
uint32_t messageCounter = 0;
unsigned long lastPoll = 0;
bool wifiLinkReady = false;

// Fetches the configured flow from the cam's /json endpoint and parses its
// "value" field. Returns true and fills *out on success; false on any
// network, parse, or device-reported error.
bool fetchCamReading(float *out) {
  if (WiFi.softAPgetStationNum() == 0) {
    Serial.println("[CV] No station joined the AP yet, skipping poll.");
    return false;
  }

  HTTPClient http;
  String url = String("http://") + SECRET_CAM_HOST + "/json";
  http.begin(url);
  http.setTimeout(5000);
  if (strlen(SECRET_CAM_USER) > 0) {
    http.setAuthorization(SECRET_CAM_USER, SECRET_CAM_PASS);
  }

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[CV] GET %s failed, HTTP status %d\n", url.c_str(), status);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[CV] JSON parse failed: %s\n", err.c_str());
    return false;
  }

  JsonVariant number = doc[SECRET_CAM_FLOW_NAME];
  if (number.isNull()) {
    Serial.printf("[CV] Flow \"%s\" not present in response.\n", SECRET_CAM_FLOW_NAME);
    return false;
  }

  const char *errorMessage = number["error"] | "";
  if (errorMessage[0] != '\0') {
    Serial.printf("[CV] Cam reported an error for \"%s\": %s\n", SECRET_CAM_FLOW_NAME, errorMessage);
    return false;
  }

  const char *valueStr = number["value"] | "";
  if (valueStr[0] == '\0') {
    Serial.printf("[CV] Flow \"%s\" has no value yet.\n", SECRET_CAM_FLOW_NAME);
    return false;
  }

  *out = atof(valueStr);
  return true;
}
}  // namespace

void cvNodeSetup() {
  WiFi.mode(WIFI_AP);

  // Reduced transmit power - needed for this C3 Super Mini.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  bool apOk = WiFi.softAP(SECRET_CAM_AP_SSID, SECRET_CAM_AP_PASSWORD, AP_CHANNEL);
  Serial.println(apOk ? "[CV] AP started" : "[CV] AP FAILED");
  Serial.printf("[CV] SSID: %s\n", SECRET_CAM_AP_SSID);
  Serial.printf("[CV] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  if (!apOk) {
    Serial.println("[CV] Cannot continue without the AP up.");
    return;
  }

  EspNowConfig espNowCfg{};
  espNowCfg.useApInterface = true;
  espNowCfg.channel = AP_CHANNEL;
  espNowCfg.sendTimeoutMs = 200;

  wifiLink = new Wifi(espNowCfg);
  if (wifiLink->init() != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW init failed.");
    return;
  }

  EspNowPeerConfig substation{};
  uint8_t substationMac[] = SECRET_MAC;
  memcpy(substation.mac, substationMac, 6);
  substation.useApInterface = true;
  if (wifiLink->addPeer(substation) != EXIT_SUCCESS) {
    Serial.println("[CV] Failed to add substation peer.");
    return;
  }

  payload.uid = DEVICE_UID;
  payload.community_id = 0;
  payload.unit_id = 0;

  wifiLinkReady = true;
}

void cvNodeLoop() {
  if (!wifiLinkReady) {
    return;
  }

  if ((millis() - lastPoll) < POLL_INTERVAL_MS) {
    return;
  }
  lastPoll = millis();

  float reading = 0.0f;
  if (!fetchCamReading(&reading)) {
    return;
  }

  payload.kwh_import = reading;
  payload.seq = messageCounter++;

  Serial.printf("[CV] reading=%.3f seq=%lu\n", reading, static_cast<unsigned long>(payload.seq));

  if (wifiLink->send(payload) != EXIT_SUCCESS) {
    Serial.println("[CV] ESP-NOW send failed.");
  }
}
