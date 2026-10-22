/**
 * @file
 * SP3485 bring up tests: the transceiver probe, the checked UART start and the
 * UART loopback that init() runs. Needs a real ESP32-C3 with the SP3485 wired as on the
 * rig (RX GPIO 1, TX GPIO 9, DE/RE GPIO 10) and powered. QEMU has no
 * transceiver to probe.
 *
 * Run with `pio test -e end_node -f test_board_sp3485` on a connected board.
 */

#include <Arduino.h>
#include <unity.h>

#include "bus_checks.h"
#include "sp3485.h"

namespace {

/** The rig's wiring, as rs485_loader.cpp gives it. */
const Rs485Config WIRED = {1, 9, 10, 9600, SERIAL_8N1};
/** The rig's wiring with RX moved to GPIO 4, which nothing drives. */
const Rs485Config RX_UNWIRED = {4, 9, 10, 9600, SERIAL_8N1};

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Releases UART1 so the next test can start it on other pins. */
void tearDown() {
  Serial1.end();
}

/**
 * @defgroup test_board_sp3485 SP3485 bring up (board)
 * @ingroup tests
 * Tests in test_board_sp3485/test_main.cpp.
 * @{
 */

/** A fitted, powered transceiver passes the probe, the UART start and the loopback. */
void test_init_succeeds_with_transceiver() {
  Sp3485 bus(WIRED, Serial1);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, bus.init());
}

/** An RX pin nothing drives floats with the pulls, so init() fails. */
void test_init_fails_with_rx_unwired() {
  Sp3485 bus(RX_UNWIRED, Serial1);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, bus.init());
}

/**
 * UART1's internal loopback echoes the test byte on the rig's pins, and leaves
 * nothing behind. QEMU does not model the loopback bit, so this runs here.
 */
void test_uart1_loopback_echoes() {
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, uartStartChecked("test", Serial1, WIRED.baudRate,
                                                       WIRED.format, WIRED.rx, WIRED.tx, false));
  pinMode(WIRED.dere, OUTPUT);
  digitalWrite(WIRED.dere, LOW);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, uartLoopbackTest("test", Serial1, UART_NUM_1, WIRED.baudRate,
                                                       WIRED.format, false, false));
  TEST_ASSERT_EQUAL_INT(0, Serial1.available());
}

/** @} */

/** Runs every test once the USB serial port is up. */
void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_init_succeeds_with_transceiver);
  RUN_TEST(test_init_fails_with_rx_unwired);
  RUN_TEST(test_uart1_loopback_echoes);
  Serial.flush();
  delay(500);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}