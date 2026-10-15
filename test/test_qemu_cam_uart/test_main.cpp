/**
 * @file
 * Tests for the CV node's wired cam link: the request line and reply framing.
 *
 * The UART itself needs real hardware, so these cover the two pieces of
 * UartCamBus that decide what goes on the wire and what comes back.
 */

#include <Arduino.h>
#include <unity.h>

#include <cstring>
#include <string>

#include "uart_cam_bus.h"

namespace {

/**
 * Builds the request line for a URL given as text.
 *
 * @param[in] url  The URL.
 * @return The line, or "" if it did not fit.
 */
std::string lineFor(const char *url) {
  char out[96];
  size_t n = camUartRequestLine(reinterpret_cast<const uint8_t *>(url), strlen(url), out, sizeof(out));
  return n == 0 ? std::string() : std::string(out, n);
}

/**
 * Feeds every byte of a string to a frame and returns the last status.
 *
 * @param[in,out] frame  Frame to feed.
 * @param[in]     bytes  Bytes to feed, in order.
 * @return Status after the last byte, or Done as soon as a frame completes.
 */
CamUartFrame::Status feedAll(CamUartFrame &frame, const std::string &bytes) {
  CamUartFrame::Status status = CamUartFrame::Pending;
  for (char c : bytes) {
    status = frame.feed(static_cast<uint8_t>(c));
    if (status == CamUartFrame::Done) {
      break;
    }
  }
  return status;
}

const std::string STX = "\x02";  ///< STX (CAM_UART_STX) as a string, for building frames.
const std::string ETX = "\x03";  ///< ETX (CAM_UART_ETX) as a string, for building frames.

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/**
 * @defgroup test_qemu_cam_uart Wired cam link
 * @ingroup tests
 * Tests in test_qemu_cam_uart/test_main.cpp.
 * @{
 */

/** The request is "GET " plus the URL's path and a newline, the host dropped. */
void test_request_line_keeps_path() {
  TEST_ASSERT_EQUAL_STRING("GET /json\n", lineFor("http://192.168.4.2/json").c_str());
}

/** A query string stays part of the path. */
void test_request_line_keeps_query() {
  TEST_ASSERT_EQUAL_STRING("GET /value?all=true\n", lineFor("http://cam/value?all=true").c_str());
}

/** A URL with no path asks for "/". */
void test_request_line_defaults_to_root() {
  TEST_ASSERT_EQUAL_STRING("GET /\n", lineFor("http://192.168.4.2").c_str());
}

/** A bare path is sent as it is. */
void test_request_line_bare_path() {
  TEST_ASSERT_EQUAL_STRING("GET /json\n", lineFor("/json").c_str());
}

/** A line that does not fit the buffer is refused rather than cut short. */
void test_request_line_too_long() {
  char out[8];
  const char *url = "http://cam/json";
  TEST_ASSERT_EQUAL_size_t(0, camUartRequestLine(reinterpret_cast<const uint8_t *>(url), strlen(url), out, sizeof(out)));
}

/** A whole frame gives its body without STX and ETX. */
void test_frame_body() {
  CamUartFrame frame;
  frame.reset();
  TEST_ASSERT_EQUAL_INT(CamUartFrame::Done, feedAll(frame, STX + "{\"main\":{}}" + ETX));
  TEST_ASSERT_EQUAL_STRING("{\"main\":{}}", frame.body());
  TEST_ASSERT_EQUAL_size_t(11, frame.length());
}

/** Cam log lines before the frame are skipped. */
void test_frame_skips_log_lines() {
  CamUartFrame frame;
  frame.reset();
  std::string bytes = "I (1234) MAINCTRL: Round #3 started\r\nW (1300) WIFI: x\r\n" + STX + "{}" + ETX;
  TEST_ASSERT_EQUAL_INT(CamUartFrame::Done, feedAll(frame, bytes));
  TEST_ASSERT_EQUAL_STRING("{}", frame.body());
}

/** Log text alone never completes a frame. */
void test_frame_log_only_is_pending() {
  CamUartFrame frame;
  frame.reset();
  TEST_ASSERT_EQUAL_INT(CamUartFrame::Pending, feedAll(frame, "I (1234) MAINCTRL: Round #3 started\r\n"));
}

/** A frame with no ETX stays pending, so the bus times out rather than using half a reply. */
void test_frame_without_etx_is_pending() {
  CamUartFrame frame;
  frame.reset();
  TEST_ASSERT_EQUAL_INT(CamUartFrame::Pending, feedAll(frame, STX + "{\"main\":"));
}

/** A second STX restarts the frame, dropping the cut off first one. */
void test_frame_restarts_on_stx() {
  CamUartFrame frame;
  frame.reset();
  TEST_ASSERT_EQUAL_INT(CamUartFrame::Done, feedAll(frame, STX + "{\"cut" + STX + "{\"whole\":1}" + ETX));
  TEST_ASSERT_EQUAL_STRING("{\"whole\":1}", frame.body());
}

/** An empty frame completes with an empty body, which the reader then rejects. */
void test_frame_empty() {
  CamUartFrame frame;
  frame.reset();
  TEST_ASSERT_EQUAL_INT(CamUartFrame::Done, feedAll(frame, STX + ETX));
  TEST_ASSERT_EQUAL_size_t(0, frame.length());
}

/** A body past CAM_UART_MAX_BODY overflows, and the next frame is still read. */
void test_frame_overflow_then_recovers() {
  CamUartFrame frame;
  frame.reset();
  std::string big(CAM_UART_MAX_BODY + 1, 'x');
  CamUartFrame::Status status = CamUartFrame::Pending;
  status = frame.feed(CAM_UART_STX);
  bool overflowed = false;
  for (char c : big) {
    status = frame.feed(static_cast<uint8_t>(c));
    overflowed = overflowed || status == CamUartFrame::Overflow;
  }
  TEST_ASSERT_TRUE(overflowed);
  TEST_ASSERT_EQUAL_INT(CamUartFrame::Done, feedAll(frame, STX + "{}" + ETX));
  TEST_ASSERT_EQUAL_STRING("{}", frame.body());
}

/** A body of exactly CAM_UART_MAX_BODY bytes is accepted. */
void test_frame_max_body_fits() {
  CamUartFrame frame;
  frame.reset();
  std::string body(CAM_UART_MAX_BODY, 'y');
  TEST_ASSERT_EQUAL_INT(CamUartFrame::Done, feedAll(frame, STX + body + ETX));
  TEST_ASSERT_EQUAL_size_t(CAM_UART_MAX_BODY, frame.length());
}

/** @} */

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_request_line_keeps_path);
  RUN_TEST(test_request_line_keeps_query);
  RUN_TEST(test_request_line_defaults_to_root);
  RUN_TEST(test_request_line_bare_path);
  RUN_TEST(test_request_line_too_long);
  RUN_TEST(test_frame_body);
  RUN_TEST(test_frame_skips_log_lines);
  RUN_TEST(test_frame_log_only_is_pending);
  RUN_TEST(test_frame_without_etx_is_pending);
  RUN_TEST(test_frame_restarts_on_stx);
  RUN_TEST(test_frame_empty);
  RUN_TEST(test_frame_overflow_then_recovers);
  RUN_TEST(test_frame_max_body_fits);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
