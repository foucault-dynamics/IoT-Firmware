#ifndef CAM_WIFI_H
#define CAM_WIFI_H

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "module.h"
#include "node_config.h"

/*
 * WiFi access point + HTTP client link to the ESP32-CAM
 * (AI-on-the-edge-device).
 *
 * The board hosts its own AP so the cam joins it directly, with no router
 * or internet needed at the meter site. Like every other Module this one
 * only moves raw bytes: send() takes the request URL and performs the GET,
 * readByte()/available() hand back the response body one byte at a time.
 * What the bytes mean is the reader's problem (lib/cam_http).
 *
 * A wired UART link (lib/esp32cam) is the planned replacement once the
 * boards are physically connected; the reader above it stays unchanged.
 */
class CamWifi : public Module {
 private:
  CamWifiConfig config;
  bool apUp = false;

  // Body of the last successful GET, drained by readByte().
  String response;
  size_t readIndex = 0;

 public:
  explicit CamWifi(const CamWifiConfig &config);
  // Deferred initialization: brings up the access point.
  void init() override;
  // Treats data as the request URL, performs the GET and buffers the body.
  int send(const uint8_t *data, size_t len) override;
  // Next byte of the buffered body, or -1 when it is drained.
  int readByte() override;
  // True while buffered body bytes remain.
  bool available() override;
  // False when the AP never came up; nothing can be polled in that state.
  bool ready() const;
};

#endif
