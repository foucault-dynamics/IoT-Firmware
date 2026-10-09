/**
 * @file
 * Radio bring up tests. Needs a real ESP32-C3, QEMU has no Wi-Fi radio.
 *
 * Run with `pio test -e unified -f test_board_radio` on a connected board.
 * The tests share one radio, so they run in order and build on each other.
 */

#include <Arduino.h>
#include <unity.h>

#include "esp_now_uplink.h"
#include "wifi_radio.h"

namespace {

/** Channel every test uses. */
constexpr uint8_t CHANNEL = 6;
/** ESP-NOW config the uplink is built with. */
const EspNowConfig ESP_NOW_CONFIG = {CHANNEL, 100};
/** The one uplink, kept alive because ESP-NOW callbacks point at it. */
EspNowUplink uplink(ESP_NOW_CONFIG);

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/** The station interface comes up on the requested channel. */
void test_station_starts() {
  TEST_ASSERT_TRUE(wifiRadioStartStation(CHANNEL));
}

/** Once the radio is on a channel, another channel is refused. */
void test_station_refuses_other_channel() {
  TEST_ASSERT_FALSE(wifiRadioStartStation(CHANNEL + 1));
}

/** ESP-NOW initialises on top of the running station. */
void test_esp_now_init_succeeds() {
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, uplink.init());
}

/** A peer can be registered once ESP-NOW is up. */
void test_esp_now_add_peer_succeeds() {
  const EspNowPeerConfig peer = {{0x24, 0x6F, 0x28, 0x01, 0x02, 0x03}};
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, uplink.addPeer(peer));
}

/** Runs every test once the USB serial port is up. */
void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_station_starts);
  RUN_TEST(test_station_refuses_other_channel);
  RUN_TEST(test_esp_now_init_succeeds);
  RUN_TEST(test_esp_now_add_peer_succeeds);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
