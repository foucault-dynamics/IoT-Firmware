#include "cam_wifi.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>

// Reduced transmit power - needed for this C3 Super Mini.
static const wifi_power_t AP_TX_POWER = WIFI_POWER_8_5dBm;

CamWifi::CamWifi(const CamWifiConfig &config) {
  this->config = config;
}

void CamWifi::init() {
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(AP_TX_POWER);

  apUp = WiFi.softAP(config.ssid, config.password, config.channel);
  Serial.println(apUp ? "[CamWifi] AP started" : "[CamWifi] AP FAILED");
  Serial.printf("[CamWifi] SSID: %s\n", config.ssid);
  Serial.printf("[CamWifi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

bool CamWifi::ready() const {
  return apUp;
}

int CamWifi::send(const uint8_t *data, size_t len) {
  response = String();
  readIndex = 0;

  if (!apUp) {
    Serial.println("[CamWifi] AP is not up, cannot reach the cam.");
    return EXIT_FAILURE;
  }

  if (WiFi.softAPgetStationNum() == 0) {
    Serial.println("[CamWifi] No station joined the AP yet, skipping request.");
    return EXIT_FAILURE;
  }

  String url;
  url.reserve(len);
  for (size_t i = 0; i < len; i++) {
    url += static_cast<char>(data[i]);
  }

  HTTPClient http;
  http.begin(url);
  http.setTimeout(config.requestTimeoutMs);
  if (strlen(config.httpUser) > 0) {
    http.setAuthorization(config.httpUser, config.httpPass);
  }

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[CamWifi] GET %s failed, HTTP status %d\n", url.c_str(), status);
    http.end();
    return EXIT_FAILURE;
  }

  response = http.getString();
  http.end();
  return EXIT_SUCCESS;
}

int CamWifi::readByte() {
  if (!available()) {
    return -1;
  }
  return static_cast<uint8_t>(response[readIndex++]);
}

bool CamWifi::available() {
  return readIndex < response.length();
}
