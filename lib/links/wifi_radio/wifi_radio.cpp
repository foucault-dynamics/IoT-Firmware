/**
 * @file
 * Radio ownership implementation.
 */

#include "wifi_radio.h"

#include <WiFi.h>
#include <esp_wifi.h>

/** Reduced transmit power, needed for the C3 SuperMini. */
static const wifi_power_t TX_POWER = WIFI_POWER_8_5dBm;

static bool apUp = false;         ///< True once the softAP has started.
static bool stationUp = false;    ///< True once the station has started.
static uint8_t radioChannel = 0;  ///< Channel the radio is on, 0 until the first start.

/**
 * Checks a start request against the channel the radio is already on.
 *
 * @param[in] channel  Requested channel.
 * @retval true   The radio is unused or already on @p channel.
 * @retval false  The radio is on a different channel. Logs the clash.
 */
static bool channelAvailable(uint8_t channel) {
  if (radioChannel != 0 && radioChannel != channel) {
    Serial.printf("[WifiRadio] Channel %u requested, radio already on %u\n",
                  channel, radioChannel);
    return false;
  }
  return true;
}

bool wifiRadioStartAp(const SoftApConfig &cfg, uint8_t channel) {
  if (!channelAvailable(channel)) {
    return false;
  }

  WiFi.enableAP(true);
  WiFi.setTxPower(TX_POWER);

  WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                     IPAddress(192, 168, 4, 1),
                     IPAddress(255, 255, 255, 0));

  apUp = WiFi.softAP(cfg.ssid, cfg.password, channel);
  if (apUp) {
    radioChannel = channel;
  }
  Serial.println(apUp ? "[WifiRadio] AP started" : "[WifiRadio] AP FAILED");
  Serial.printf("[WifiRadio] SSID: %s\n", cfg.ssid);
  Serial.printf("[WifiRadio] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  return apUp;
}

bool wifiRadioStartStation(uint8_t channel) {
  if (!channelAvailable(channel)) {
    return false;
  }
  if (stationUp) {
    return true;
  }

  WiFi.enableSTA(true);
  WiFi.setTxPower(TX_POWER);

  stationUp = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
  if (stationUp) {
    radioChannel = channel;
  }
  Serial.println(stationUp ? "[WifiRadio] Station started" : "[WifiRadio] Station FAILED");
  Serial.printf("[WifiRadio] Channel: %u\n", channel);

  return stationUp;
}

bool wifiRadioApUp() {
  return apUp;
}

bool wifiRadioHasStations() {
  return WiFi.softAPgetStationNum() > 0;
}
