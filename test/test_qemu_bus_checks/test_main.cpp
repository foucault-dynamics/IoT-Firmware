/**
 * @file
 * Tests for the shared bus checks in bus_checks.h, run in QEMU.
 *
 * Covers how a pin's levels under each pull are classified, which drive counts
 * as an idle UART RX line for each polarity, the checked UART start, and the
 * loopback test refusing an unknown UART. The pin reads themselves need real
 * hardware, and QEMU does not model the UART's loopback bit, so
 * test_board_sp3485 and test_board_ir_head cover those.
 */

#include <Arduino.h>
#include <unity.h>

#include "bus_checks.h"

namespace {

/** Free UART1 pins on the ESP32-C3. */
///@{
constexpr uint8_t RX_PIN = 6;
constexpr uint8_t TX_PIN = 7;
///@}
/** A GPIO the ESP32-C3 does not have, so the UART cannot attach to it. */
constexpr uint8_t BAD_PIN = 30;

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Releases UART1 so the next test starts it fresh. */
void tearDown() {
  Serial1.end();
}

/**
 * @defgroup test_qemu_bus_checks Bus checks
 * @ingroup tests
 * Tests in test_qemu_bus_checks/test_main.cpp.
 * @{
 */

/** HIGH under both pulls means something drives the pin high. */
void test_classify_driven_high() {
  TEST_ASSERT_TRUE(classifyPin(HIGH, HIGH) == PinDrive::DrivenHigh);
}

/** LOW under both pulls means something drives the pin low. */
void test_classify_driven_low() {
  TEST_ASSERT_TRUE(classifyPin(LOW, LOW) == PinDrive::DrivenLow);
}

/** Following each pull means nothing drives the pin. */
void test_classify_floating() {
  TEST_ASSERT_TRUE(classifyPin(HIGH, LOW) == PinDrive::Floating);
}

/** Going against each pull means the pin cannot be trusted. */
void test_classify_unstable() {
  TEST_ASSERT_TRUE(classifyPin(LOW, HIGH) == PinDrive::Unstable);
}

/** A normal RX line idles driven high. */
void test_rx_idle_normal_driven_high_passes() {
  TEST_ASSERT_TRUE(rxDriveIsIdle(PinDrive::DrivenHigh, false));
}

/** A normal RX line held low is active, not idle. */
void test_rx_idle_normal_driven_low_fails() {
  TEST_ASSERT_FALSE(rxDriveIsIdle(PinDrive::DrivenLow, false));
}

/** A normal RX line that floats has nothing on it. */
void test_rx_idle_normal_floating_fails() {
  TEST_ASSERT_FALSE(rxDriveIsIdle(PinDrive::Floating, false));
}

/** A normal RX line that is unstable fails. */
void test_rx_idle_normal_unstable_fails() {
  TEST_ASSERT_FALSE(rxDriveIsIdle(PinDrive::Unstable, false));
}

/** An inverted RX line idles driven low. */
void test_rx_idle_inverted_driven_low_passes() {
  TEST_ASSERT_TRUE(rxDriveIsIdle(PinDrive::DrivenLow, true));
}

/** An inverted RX line held high is active, not idle. */
void test_rx_idle_inverted_driven_high_fails() {
  TEST_ASSERT_FALSE(rxDriveIsIdle(PinDrive::DrivenHigh, true));
}

/** An inverted RX line that floats has nothing on it. */
void test_rx_idle_inverted_floating_fails() {
  TEST_ASSERT_FALSE(rxDriveIsIdle(PinDrive::Floating, true));
}

/** An inverted RX line that is unstable fails. */
void test_rx_idle_inverted_unstable_fails() {
  TEST_ASSERT_FALSE(rxDriveIsIdle(PinDrive::Unstable, true));
}

/** UART1 starts on valid pins. */
void test_uart_start_valid_pins_passes() {
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,
                        uartStartChecked("test", Serial1, 9600, SERIAL_8N1, RX_PIN, TX_PIN, false));
  TEST_ASSERT_TRUE(static_cast<bool>(Serial1));
}

/** UART1 refuses a GPIO that does not exist, and is left stopped. */
void test_uart_start_bad_pin_fails() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE,
                        uartStartChecked("test", Serial1, 9600, SERIAL_8N1, BAD_PIN, TX_PIN, false));
  TEST_ASSERT_FALSE(static_cast<bool>(Serial1));
}

/** Serial1 maps to UART1, and Serial, which is UART0 with USB CDC off, to UART0. */
void test_uart_port_of() {
  TEST_ASSERT_EQUAL_INT(UART_NUM_1, uartPortOf(Serial1));
  TEST_ASSERT_EQUAL_INT(UART_NUM_0, uartPortOf(Serial));
}

/** An unknown UART fails the loopback test without touching anything. */
void test_loopback_unknown_port_fails() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE,
                        uartLoopbackTest("test", Serial1, UART_NUM_MAX, 9600, SERIAL_8N1, false, false));
}

/** @} */

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_classify_driven_high);
  RUN_TEST(test_classify_driven_low);
  RUN_TEST(test_classify_floating);
  RUN_TEST(test_classify_unstable);
  RUN_TEST(test_rx_idle_normal_driven_high_passes);
  RUN_TEST(test_rx_idle_normal_driven_low_fails);
  RUN_TEST(test_rx_idle_normal_floating_fails);
  RUN_TEST(test_rx_idle_normal_unstable_fails);
  RUN_TEST(test_rx_idle_inverted_driven_low_passes);
  RUN_TEST(test_rx_idle_inverted_driven_high_fails);
  RUN_TEST(test_rx_idle_inverted_floating_fails);
  RUN_TEST(test_rx_idle_inverted_unstable_fails);
  RUN_TEST(test_uart_start_valid_pins_passes);
  RUN_TEST(test_uart_start_bad_pin_fails);
  RUN_TEST(test_uart_port_of);
  RUN_TEST(test_loopback_unknown_port_fails);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
