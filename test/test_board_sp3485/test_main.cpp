/**
 * @file
 * SP3485 probe tests. Needs a real ESP32-C3 with the SP3485 wired as on the
 * rig (RX GPIO 8, TX GPIO 9, DE/RE GPIO 10) and powered. QEMU has no
 * transceiver to probe.
 *
 * Run with `pio test -e end_node -f test_board_sp3485` on a connected board.
 */

#include <Arduino.h>
#include <unity.h>

#include "sp3485.h"

namespace {

/** The rig's wiring, as rs485_loader.cpp gives it. */
const Rs485Config WIRED = {8, 9, 10, 9600, SERIAL_8N1};
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
 * @defgroup test_board_sp3485 SP3485 probe (board)
 * @ingroup tests
 * Tests in test_board_sp3485/test_main.cpp.
 * @{
 */

/** A fitted, powered transceiver passes the probe. */
void test_init_succeeds_with_transceiver() {
  Sp3485 bus(WIRED, Serial1);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, bus.init());
}

/** An RX pin nothing drives floats with the pulls, so init() fails. */
void test_init_fails_with_rx_unwired() {
  Sp3485 bus(RX_UNWIRED, Serial1);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, bus.init());
}

/** @} */

/** Runs every test once the USB serial port is up. */
void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_init_succeeds_with_transceiver);
  RUN_TEST(test_init_fails_with_rx_unwired);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}