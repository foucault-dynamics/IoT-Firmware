#include "wifi_radio.h"

#include <WiFi.h>

// Reduced transmit power - needed for this C3 Super Mini.
static const wifi_power_t AP_TX_POWER = WIFI_POWER_8_5dBm;

static bool apUp = false;

bool wifiRadioStart(const WifiRadioConfig &cfg) {
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(AP_TX_POWER);

  WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                     IPAddress(192, 168, 4, 1),
                     IPAddress(255, 255, 255, 0));

  apUp = WiFi.softAP(cfg.ssid, cfg.password, cfg.channel);
  Serial.println(apUp ? "[WifiRadio] AP started" : "[WifiRadio] AP FAILED");
  Serial.printf("[WifiRadio] SSID: %s\n", cfg.ssid);
  Serial.printf("[WifiRadio] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  return apUp;
}

bool wifiRadioUp() {
  return apUp;
}

bool wifiRadioHasStations() {
  return WiFi.softAPgetStationNum() > 0;
}
