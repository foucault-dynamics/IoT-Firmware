/**
 * @file
 * UartCamBus, the wired Module the CV node can use instead of HttpBus.
 *
 * Carries the same request and reply as HttpBus, over a UART wired to the
 * ESP32-CAM, so CamHttpReader works on either without a change. The wire
 * protocol is in docs/CV_WIRED_LINK.md:
 *
 * - The C3 sends one ASCII line, "GET <path>\n", for example "GET /json\n".
 * - The cam answers STX (0x02), the body its HTTP server would send for that
 *   path, then ETX (0x03).
 *
 * Bytes outside STX and ETX are ignored, so the cam's log console can share
 * the same TX pin.
 *
 * @warning Prototype. The cam's stock AI-on-the-edge-device firmware does not
 *          answer on its UART, so this needs the cam side described in
 *          docs/CV_WIRED_LINK.md, on the cam fork's `wired-json-uart` branch.
 *          Each side has run on hardware on its own (test_board_cam_uart on a
 *          C3, the cam answering CamSim/cam_uart_poll.py), but the two have
 *          not yet been wired to each other.
 */

#ifndef UART_CAM_BUS_H
#define UART_CAM_BUS_H

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "module.h"
#include "cv_config.h"

/** Start of a reply frame. */
#define CAM_UART_STX 0x02
/** End of a reply frame. */
#define CAM_UART_ETX 0x03
/** Longest reply body accepted, in bytes. The cam's /json is a few hundred. */
#define CAM_UART_MAX_BODY 2048

/**
 * Builds the request line for a URL.
 *
 * Takes what CamHttpReader hands to Module::send(), a full URL such as
 * "http://192.168.4.2/json", and keeps only the path. The host means nothing on
 * a wire. A URL with no path asks for "/".
 *
 * @param[in]  url     The URL, not null terminated.
 * @param[in]  len     Length of @p url in bytes.
 * @param[out] out     The line, "GET <path>\n", null terminated.
 * @param[in]  outLen  Size of @p out in bytes.
 * @return Length of the line without its terminator, or 0 if @p out is too
 *         small.
 */
size_t camUartRequestLine(const uint8_t *url, size_t len, char *out, size_t outLen);

/**
 * Picks one reply frame out of a byte stream.
 *
 * Feed it every byte read from the UART. Bytes before an STX are skipped, an
 * STX starts a fresh frame even in the middle of one (the cam restarted a
 * reply), and ETX closes the frame.
 */
class CamUartFrame {
 public:
  /** What the last byte did to the frame. */
  enum Status {
    Pending,   ///< No complete frame yet.
    Done,      ///< ETX arrived, body() holds the frame.
    Overflow,  ///< The body passed CAM_UART_MAX_BODY. Waits for the next STX.
  };

  /** Forgets any partial frame. */
  void reset();

  /**
   * Takes the next byte from the UART.
   *
   * @param[in] b  The byte.
   * @return What the frame looks like after it.
   */
  Status feed(uint8_t b);

  /**
   * The body of the last complete frame, without STX and ETX.
   *
   * @return The body, null terminated. Only valid after feed() returned Done.
   */
  const char *body() const { return buf; }

  /**
   * Length of body().
   *
   * @return Length in bytes.
   */
  size_t length() const { return len; }

 private:
  char buf[CAM_UART_MAX_BODY + 1];  ///< Body so far, null terminated once Done.
  size_t len = 0;                   ///< Bytes in #buf.
  bool inFrame = false;             ///< True between STX and ETX.
};

/**
 * Wired link to the ESP32-CAM over one of the C3's hardware UARTs.
 *
 * Like HttpBus it only moves bytes: send() takes the request URL, sends the
 * request line and waits for the reply frame, and readByte() and available()
 * hand back its body. What the bytes mean is CamHttpReader's job.
 */
class UartCamBus : public Module {
 private:
  UartCamConfig config;    ///< Pins, rate and reply timeout.
  HardwareSerial *serial;  ///< UART wired to the cam.

  CamUartFrame frame;      ///< Reply being received.
  String response;         ///< Body of the last good reply, drained by readByte().
  size_t readIndex = 0;    ///< Next byte of #response to hand out.

 public:
  /**
   * Stores the config. The UART is not started until init().
   *
   * @param[in] config  Pins, rate and reply timeout, copied.
   * @param[in] serial  Hardware UART to use. Must outlive this object.
   */
  UartCamBus(const UartCamConfig &config, HardwareSerial &serial);

  /**
   * Starts the UART on the configured pins, 8N1.
   *
   * The cam may still be booting, so this does not talk to it. The CV node's
   * readiness probe does that.
   *
   * @retval EXIT_SUCCESS  The UART is running.
   * @retval EXIT_FAILURE  The rate is 0, or RX and TX are the same pin.
   */
  int init() override;

  /**
   * Sends the request line for a URL and waits for the reply frame.
   *
   * Drops anything already waiting first, such as cam log lines, and discards
   * the previous body even if this request fails.
   *
   * @param[in] data  Request URL, not null terminated.
   * @param[in] len   Length of the URL in bytes.
   * @retval EXIT_SUCCESS  A whole frame arrived, its body is ready to read.
   * @retval EXIT_FAILURE  No frame before UartCamConfig::timeoutMs, or the
   *                       body was too long.
   */
  int send(const uint8_t *data, size_t len) override;

  /**
   * Hands out the next byte of the reply body.
   *
   * @return The byte (0 to 255), or -1 once the body is drained.
   */
  int readByte() override;

  /**
   * Reports whether any of the reply body is left.
   *
   * @retval true   readByte() has more bytes.
   * @retval false  The body is drained, or no request has succeeded.
   */
  bool available() override;
};

#endif
