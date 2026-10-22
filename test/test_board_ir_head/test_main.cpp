/**
 * @file
 * RealIrHead bring up tests: the RX idle check, the checked UART start and the
 * UART loopback that init() runs. Needs a real ESP32-C3 with the PiggyMeter
 * head wired as ir_loader.cpp gives it (RX GPIO 20, TX GPIO 21, ir_rx_inv 0),
 * powered and kept out of bright light. QEMU has no head to probe.
 *
 * Run with `pio test -e end_node -f test_board_ir_head` on a connected board.
 */

#include <Arduino.h>
#include <unity.h>

#include "bus_checks.h"
#include "real_ir_head.h"

namespace {

/** The rig's wiring, as ir_loader.cpp gives it: 300 baud 7E1, no inversion. */
const IrConfig WIRED = {20, 21, 300, SERIAL_7E1, false, false};
/** The rig's wiring with RX moved to GPIO 4, which nothing drives. */
const IrConfig RX_UNWIRED = {4, 21, 300, SERIAL_7E1, false, false};

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Releases UART1 so the next test can start it on other pins. */
void tearDown() {
  Serial1.end();
}

/**
 * @defgroup test_board_ir_head IR head bring up (board)
 * @ingroup tests
 * Tests in test_board_ir_head/test_main.cpp.
 * @{
 */

/** A fitted, powered head in the dark passes the RX check, the UART start and the loopback. */
void test_init_succeeds_with_head() {
  RealIrHead head(WIRED, Serial1);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, head.init());
}

/** An RX pin nothing drives floats with the pulls, so init() fails. */
void test_init_fails_with_rx_unwired() {
  RealIrHead head(RX_UNWIRED, Serial1);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, head.init());
}

/** UART1's internal loopback echoes the test byte at 300 baud 7E1, and leaves nothing behind. */
void test_uart1_loopback_echoes() {
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, uartStartChecked("test", Serial1, WIRED.baudRate,
                                                       WIRED.format, WIRED.rx, WIRED.tx, false));
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, uartLoopbackTest("test", Serial1, UART_NUM_1, WIRED.baudRate,
                                                       WIRED.format, false, false));
  TEST_ASSERT_EQUAL_INT(0, Serial1.available());
}

/** @} */

/** Runs every test once the USB serial port is up. */
void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_init_succeeds_with_head);
  RUN_TEST(test_init_fails_with_rx_unwired);
  RUN_TEST(test_uart1_loopback_echoes);
  Serial.flush();
  delay(500);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
