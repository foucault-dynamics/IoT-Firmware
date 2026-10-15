/**
 * @file
 * UartCamBus tests on a real ESP32-C3, with no wiring.
 *
 * UART1 is put in internal loopback, so everything it sends comes straight
 * back to its own RX. A FreeRTOS task plays the cam: shortly after each
 * request it writes the reply frame to the same UART, which loops back into
 * UartCamBus. This runs the real UART, pins and timing that QEMU cannot,
 * without the cam or a USB to serial adapter.
 *
 * Run with `pio test -e end_node -f test_board_cam_uart` on a connected board.
 */

#include <Arduino.h>
#include <unity.h>

#include <cstring>

#include "driver/uart.h"

#include "cam_http.h"
#include "uart_cam_bus.h"

namespace {

/** The CV node's default wiring, with a short timeout to keep the tests quick. */
const UartCamConfig CONFIG = {20, 21, 115200, 500};

/** What the fake cam writes back, set by each test before its request. */
const char *reply = nullptr;

/** Fake cam: waits for the request to go out, then writes #reply into the loopback. */
void fakeCam(void *) {
  vTaskDelay(pdMS_TO_TICKS(30));
  if (reply != nullptr) {
    Serial1.write(reinterpret_cast<const uint8_t *>(reply), strlen(reply));
  }
  vTaskDelete(nullptr);
}

/**
 * Starts the fake cam, then sends one request through the bus.
 *
 * @param[in,out] bus      Bus under test, already initialised.
 * @param[in]     answer   Bytes the fake cam answers with, or nullptr for silence.
 * @return What UartCamBus::send() returned.
 */
int requestWith(UartCamBus &bus, const char *answer) {
  reply = answer;
  xTaskCreate(fakeCam, "fakeCam", 2048, nullptr, 1, nullptr);
  const char *url = "http://192.168.4.2/json";
  return bus.send(reinterpret_cast<const uint8_t *>(url), strlen(url));
}

/**
 * Drains the bus's reply body into a string.
 *
 * @param[in,out] bus  Bus to drain.
 * @return The body.
 */
String drain(UartCamBus &bus) {
  String out;
  while (bus.available()) {
    out += static_cast<char>(bus.readByte());
  }
  return out;
}

}  // namespace

/** Nothing to set up, each test starts its own bus. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/**
 * @defgroup test_board_cam_uart Wired cam link (board)
 * @ingroup tests
 * Tests in test_board_cam_uart/test_main.cpp.
 * @{
 */

/** A framed reply comes back as the body, without STX and ETX. */
void test_framed_reply_is_read() {
  UartCamBus bus(CONFIG, Serial1);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, bus.init());
  uart_set_loop_back(UART_NUM_1, true);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, requestWith(bus, "\x02{\"main\":{}}\x03"));
  TEST_ASSERT_EQUAL_STRING("{\"main\":{}}", drain(bus).c_str());
}

/** Log text around the frame, and the echoed request itself, are skipped. */
void test_log_text_is_skipped() {
  UartCamBus bus(CONFIG, Serial1);
  bus.init();
  uart_set_loop_back(UART_NUM_1, true);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, requestWith(bus, "I (12) MAINCTRL: Round\r\n\x02{}\x03W (13) x\r\n"));
  TEST_ASSERT_EQUAL_STRING("{}", drain(bus).c_str());
}

/** No reply fails once the timeout has passed, not before. */
void test_silence_times_out() {
  UartCamBus bus(CONFIG, Serial1);
  bus.init();
  uart_set_loop_back(UART_NUM_1, true);
  unsigned long start = millis();
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, requestWith(bus, nullptr));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(CONFIG.timeoutMs, millis() - start);
  TEST_ASSERT_FALSE(bus.available());
}

/** A frame cut off before ETX fails rather than handing back half a reply. */
void test_missing_etx_fails() {
  UartCamBus bus(CONFIG, Serial1);
  bus.init();
  uart_set_loop_back(UART_NUM_1, true);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, requestWith(bus, "\x02{\"main\":"));
}

/** CamHttpReader reads the import value through the wired bus unchanged. */
void test_reader_over_uart() {
  UartCamBus bus(CONFIG, Serial1);
  bus.init();
  uart_set_loop_back(UART_NUM_1, true);
  CamHttpConfig cfg{};
  strncpy(cfg.host, "192.168.4.2", sizeof(cfg.host) - 1);
  strncpy(cfg.path, "/json", sizeof(cfg.path) - 1);
  strncpy(cfg.flowName, "main", sizeof(cfg.flowName) - 1);
  CamHttpReader reader(cfg);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.init(bus));
  reply = "\x02{\"main\":{\"value\":\"531.82235\",\"error\":\"no error\"}}\x03";
  xTaskCreate(fakeCam, "fakeCam", 2048, nullptr, 1, nullptr);
  float val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_import(&val));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 531.82235f, val);
}

/** @} */

/** Runs every test once the USB serial port is up. */
void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_framed_reply_is_read);
  RUN_TEST(test_log_text_is_skipped);
  RUN_TEST(test_silence_times_out);
  RUN_TEST(test_missing_etx_fails);
  RUN_TEST(test_reader_over_uart);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
