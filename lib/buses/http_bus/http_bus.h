/**
 * @file
 * HttpBus, the HTTP client Module the CV node uses to reach the ESP32-CAM.
 */

#ifndef HTTP_BUS_H
#define HTTP_BUS_H

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "module.h"
#include "cv_config.h"

/**
 * HTTP client link to the ESP32-CAM (AI-on-the-edge-device), carried over the
 * AP that wifi_radio.h hosts.
 *
 * The board hosts its own AP so the cam joins it directly, with no router or
 * internet needed at the meter site. Like every other Module this one only
 * moves raw bytes: send() takes the request URL and performs the GET, and
 * readByte() and available() hand back the response body one byte at a time.
 * What the bytes mean is CamHttpReader's job.
 *
 * @todo Replace with a wired UART link once the boards are physically
 *       connected. CamHttpReader above it stays unchanged.
 */
class HttpBus : public Module {
 private:
  HttpBusConfig config;  ///< Timeout and credentials.

  String response;       ///< Body of the last successful GET, drained by readByte().
  size_t readIndex = 0;  ///< Next byte of #response to hand out.

 public:
  /**
   * Stores the config. No network activity happens until send().
   *
   * @param[in] config  Timeout and credentials, copied.
   */
  explicit HttpBus(const HttpBusConfig &config);

  /**
   * Does nothing, because wifi_radio.h owns bringing the AP up.
   *
   * @retval EXIT_SUCCESS  Always.
   */
  int init() override;

  /**
   * Performs a GET and buffers the response body for readByte().
   *
   * Any previous body is discarded first, even if this request fails.
   *
   * @param[in] data  Request URL, not null terminated.
   * @param[in] len   Length of the URL in bytes.
   * @retval EXIT_SUCCESS  Got HTTP 200, the body is ready to read.
   * @retval EXIT_FAILURE  The AP is down, nothing has joined it, or the GET
   *                       failed or returned another status.
   */
  int send(const uint8_t *data, size_t len) override;

  /**
   * Hands out the next byte of the buffered response body.
   *
   * @return The byte (0 to 255), or -1 once the body is drained.
   */
  int readByte() override;

  /**
   * Reports whether any of the buffered response body is left.
   *
   * @retval true   readByte() has more bytes.
   * @retval false  The body is drained, or no GET has succeeded.
   */
  bool available() override;
};

#endif
