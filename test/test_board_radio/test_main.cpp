/**
 * @file
 * Radio bring up and send failure tests. Needs a real ESP32-C3, QEMU has no
 * Wi-Fi radio.
 *
 * Run with `pio test -e end_node -f test_board_radio` on a connected board.
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
/** A registered peer with no board behind it, so nothing ever ACKs. */
const EspNowPeerConfig ABSENT_PEER = {{0x24, 0x6F, 0x28, 0x01, 0x02, 0x03}};
/** A MAC never passed to addPeer(). */
const uint8_t UNREGISTERED_MAC[6] = {0x24, 0x6F, 0x28, 0x0A, 0x0B, 0x0C};

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
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, uplink.addPeer(ABSENT_PEER));
}

/** A send that no board acknowledges is reported as failed, not delivered. */
void test_send_to_absent_peer_fails() {
  const uint8_t packet[34] = {0};
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, uplink.sendPacket(ABSENT_PEER.mac, packet, sizeof(packet)));
}

/** A send to a MAC that was never added as a peer fails. */
void test_send_to_unregistered_peer_fails() {
  const uint8_t packet[34] = {0};
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, uplink.sendPacket(UNREGISTERED_MAC, packet, sizeof(packet)));
}

/** A packet larger than one ESP-NOW frame, or an empty one, is refused before sending. */
void test_send_rejects_bad_length() {
  static uint8_t oversized[ESP_NOW_MAX_DATA_LEN + 1] = {0};
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, uplink.sendPacket(ABSENT_PEER.mac, oversized, sizeof(oversized)));
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, uplink.sendPacket(ABSENT_PEER.mac, oversized, 0));
}

/** Runs every test once the USB serial port is up. */
void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_station_starts);
  RUN_TEST(test_station_refuses_other_channel);
  RUN_TEST(test_esp_now_init_succeeds);
  RUN_TEST(test_esp_now_add_peer_succeeds);
  RUN_TEST(test_send_to_absent_peer_fails);
  RUN_TEST(test_send_to_unregistered_peer_fails);
  RUN_TEST(test_send_rejects_bad_length);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
