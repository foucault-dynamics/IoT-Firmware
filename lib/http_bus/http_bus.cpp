#include "http_bus.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>

#include "wifi_radio.h"

HttpBus::HttpBus(const HttpBusConfig &config) {
  this->config = config;
}

void HttpBus::init() {
}

int HttpBus::send(const uint8_t *data, size_t len) {
  response = String();
  readIndex = 0;

  if (!wifiRadioUp()) {
    Serial.println("[HttpBus] AP is not up, cannot reach the cam.");
    return EXIT_FAILURE;
  }

  if (!wifiRadioHasStations()) {
    Serial.println("[HttpBus] No station joined the AP yet, skipping request.");
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
    Serial.printf("[HttpBus] GET %s failed, HTTP status %d\n", url.c_str(), status);
    http.end();
    return EXIT_FAILURE;
  }

  response = http.getString();
  http.end();
  return EXIT_SUCCESS;
}

int HttpBus::readByte() {
  if (!available()) {
    return -1;
  }
  return static_cast<uint8_t>(response[readIndex++]);
}

bool HttpBus::available() {
  return readIndex < response.length();
}
