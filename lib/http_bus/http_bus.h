#ifndef HTTP_BUS_H
#define HTTP_BUS_H

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "module.h"
#include "node_config.h"

/*
 * HTTP client link to the ESP32-CAM (AI-on-the-edge-device), carried over
 * the AP that lib/wifi_radio hosts.
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
class HttpBus : public Module {
 private:
  HttpBusConfig config;

  // Body of the last successful GET, drained by readByte().
  String response;
  size_t readIndex = 0;

 public:
  explicit HttpBus(const HttpBusConfig &config);
  // No-op: the radio (lib/wifi_radio) owns bringing the AP up.
  void init() override;
  // Treats data as the request URL, performs the GET and buffers the body.
  int send(const uint8_t *data, size_t len) override;
  // Next byte of the buffered body, or -1 when it is drained.
  int readByte() override;
  // True while buffered body bytes remain.
  bool available() override;
};

#endif
