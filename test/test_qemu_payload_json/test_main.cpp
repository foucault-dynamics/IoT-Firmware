/**
 * @file
 * Tests for the gateway's MQTT JSON and its ISO 8601 timestamp, run in QEMU.
 */

#include <Arduino.h>
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "payload_json.h"

namespace {

/** 2026-10-07 14:03:00 in AEST, the gateway's timezone. */
constexpr time_t SYNCED_NOW = 1791345780;
/** A clock that NTP has not set yet, one hour after boot at the epoch. */
constexpr time_t UNSYNCED_NOW = 3600;

/**
 * Builds the reading every JSON test serialises.
 *
 * @return UID 00 to 0F, seq 42, 1234.5 kWh in, 45.25 kWh out, 230 V, community 7, unit 9.
 */
Payload knownPayload() {
  Payload p{};
  for (size_t i = 0; i < UID_LEN; i++) {
    p.uid[i] = static_cast<uint8_t>(i);
  }
  p.seq = 42;
  p.kwh_import = 1234.5f;
  p.kwh_export = 45.25f;
  p.voltage = 230.0f;
  p.community_id = 7;
  p.unit_id = 9;
  return p;
}

}  // namespace

/** Sets the timezone the gateway sets with configTzTime(). */
void setUp() {
  setenv("TZ", "AEST-10", 1);
  tzset();
}

/** Nothing to clean up. */
void tearDown() {}

/** A synced clock formats as local time with a +10:00 offset. */
void test_timestamp_iso_8601() {
  char out[TIMESTAMP_LEN];
  TEST_ASSERT_TRUE(formatTimestamp(SYNCED_NOW, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("2026-10-07T14:03:00+10:00", out);
}

/** A clock before MIN_VALID_EPOCH means NTP has not synced, so there is no timestamp. */
void test_timestamp_unsynced_fails() {
  char out[TIMESTAMP_LEN];
  TEST_ASSERT_FALSE(formatTimestamp(UNSYNCED_NOW, out, sizeof(out)));
  TEST_ASSERT_FALSE(formatTimestamp(MIN_VALID_EPOCH - 1, out, sizeof(out)));
}

/** The timestamp needs 26 bytes, so 25 fails and 26 fits. */
void test_timestamp_buffer_size() {
  char out[26];
  TEST_ASSERT_FALSE(formatTimestamp(SYNCED_NOW, out, 25));
  TEST_ASSERT_TRUE(formatTimestamp(SYNCED_NOW, out, 26));
  TEST_ASSERT_EQUAL_STRING("2026-10-07T14:03:00+10:00", out);
}

/** A known reading serialises to exactly the published JSON, in field order. */
void test_json_exact() {
  char out[320];
  size_t len = buildPayloadJson(knownPayload(), -71, 9.5f, SYNCED_NOW, out, sizeof(out));
  const char *expected =
      "{\"ts\":\"2026-10-07T14:03:00+10:00\",\"uid\":\"000102030405060708090a0b0c0d0e0f\",\"seq\":42,"
      "\"kwh_import\":1234.5,\"kwh_export\":45.25,\"voltage\":230,\"community_id\":7,\"unit_id\":9,"
      "\"rssi\":-71,\"snr\":9.5}";
  TEST_ASSERT_EQUAL_STRING(expected, out);
  TEST_ASSERT_EQUAL_size_t(strlen(expected), len);
}

/** Before NTP syncs, "ts" is null and the rest of the reading is still published. */
void test_json_ts_null_before_sync() {
  char out[320];
  buildPayloadJson(knownPayload(), -71, 9.5f, UNSYNCED_NOW, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_LEN("{\"ts\":null,\"uid\":\"000102030405060708090a0b0c0d0e0f\",\"seq\":42,",
                               out, strlen("{\"ts\":null,\"uid\":\"000102030405060708090a0b0c0d0e0f\",\"seq\":42,"));
}

/** The largest reading fits the gateway's 320 byte buffer with room to spare. */
void test_json_fits_gateway_buffer() {
  Payload p = knownPayload();
  memset(p.uid, 0xFF, UID_LEN);
  p.seq = 0xFFFFFFFF;
  p.kwh_import = -3.4e38f;
  p.kwh_export = -3.4e38f;
  p.voltage = -3.4e38f;
  p.community_id = 255;
  p.unit_id = 255;
  char out[320];
  size_t len = buildPayloadJson(p, -32768, -3.4e38f, SYNCED_NOW, out, sizeof(out));
  TEST_ASSERT_LESS_THAN_size_t(sizeof(out) - 1, len);
}

/** A buffer too small for the JSON is filled to the last byte with no terminator, so only the return value shows it. */
void test_json_small_buffer_fills_without_terminator() {
  char out[65];
  memset(out, 'X', sizeof(out));
  size_t len = buildPayloadJson(knownPayload(), -71, 9.5f, SYNCED_NOW, out, 64);
  TEST_ASSERT_EQUAL_size_t(64, len);
  TEST_ASSERT_EQUAL_CHAR('X', out[64]);
  TEST_ASSERT_EQUAL_STRING_LEN("{\"ts\":\"2026-10-07T14:03:00+10:00\"", out, 33);
}

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_timestamp_iso_8601);
  RUN_TEST(test_timestamp_unsynced_fails);
  RUN_TEST(test_timestamp_buffer_size);
  RUN_TEST(test_json_exact);
  RUN_TEST(test_json_ts_null_before_sync);
  RUN_TEST(test_json_fits_gateway_buffer);
  RUN_TEST(test_json_small_buffer_fills_without_terminator);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}