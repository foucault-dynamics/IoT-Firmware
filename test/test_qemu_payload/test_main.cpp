/**
 * @file
 * Tests for the over the air packet formats in shared_payload.h, run in QEMU.
 *
 * Pins the size, field offsets and little endian byte image of Payload and
 * AckPayload, so a change to either struct fails here before it reaches a
 * board that would reject the other boards' packets. Also covers the UID
 * helpers.
 */

#include <Arduino.h>
#include <esp_now.h>
#include <unity.h>

#include <cstddef>
#include <cstring>

#include "shared_payload.h"

static_assert(sizeof(Payload) == 34, "Payload is 34 bytes on the air");
static_assert(sizeof(AckPayload) == 20, "AckPayload is 20 bytes on the air");
static_assert(sizeof(Payload) <= ESP_NOW_MAX_DATA_LEN, "Payload fits one ESP-NOW frame");

namespace {

/** Largest LoRa packet the SX1276 FIFO holds, in bytes. */
constexpr size_t LORA_MAX_PACKET = 255;

/**
 * Fills a UID with 0x00, 0x01, ... 0x0F.
 *
 * @param[out] uid  UID_LEN bytes to fill.
 */
void countingUid(uint8_t *uid) {
  for (size_t i = 0; i < UID_LEN; i++) {
    uid[i] = static_cast<uint8_t>(i);
  }
}

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/** Payload is 34 bytes and AckPayload 20, with no padding. */
void test_sizes() {
  TEST_ASSERT_EQUAL_size_t(34, sizeof(Payload));
  TEST_ASSERT_EQUAL_size_t(20, sizeof(AckPayload));
}

/** Payload's fields sit at fixed offsets: uid, seq, import, export, voltage, community, unit. */
void test_payload_field_offsets() {
  TEST_ASSERT_EQUAL_size_t(0, offsetof(Payload, uid));
  TEST_ASSERT_EQUAL_size_t(16, offsetof(Payload, seq));
  TEST_ASSERT_EQUAL_size_t(20, offsetof(Payload, kwh_import));
  TEST_ASSERT_EQUAL_size_t(24, offsetof(Payload, kwh_export));
  TEST_ASSERT_EQUAL_size_t(28, offsetof(Payload, voltage));
  TEST_ASSERT_EQUAL_size_t(32, offsetof(Payload, community_id));
  TEST_ASSERT_EQUAL_size_t(33, offsetof(Payload, unit_id));
}

/** AckPayload carries the uid then the seq, the same prefix as Payload. */
void test_ack_field_offsets() {
  TEST_ASSERT_EQUAL_size_t(0, offsetof(AckPayload, uid));
  TEST_ASSERT_EQUAL_size_t(16, offsetof(AckPayload, seq));
}

/** Both packets fit one ESP-NOW frame and one LoRa packet. */
void test_packets_fit_both_links() {
  TEST_ASSERT_LESS_OR_EQUAL_size_t(ESP_NOW_MAX_DATA_LEN, sizeof(Payload));
  TEST_ASSERT_LESS_OR_EQUAL_size_t(LORA_MAX_PACKET, sizeof(Payload));
  TEST_ASSERT_LESS_OR_EQUAL_size_t(LORA_MAX_PACKET, sizeof(AckPayload));
}

/** A known Payload serialises to the expected little endian bytes. */
void test_payload_byte_image() {
  Payload p{};
  countingUid(p.uid);
  p.seq = 0x01020304;
  p.kwh_import = 1234.5f;
  p.kwh_export = 45.25f;
  p.voltage = 230.0f;
  p.community_id = 7;
  p.unit_id = 9;

  const uint8_t expected[34] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
      0x04, 0x03, 0x02, 0x01,
      0x00, 0x50, 0x9A, 0x44,
      0x00, 0x00, 0x35, 0x42,
      0x00, 0x00, 0x66, 0x43,
      0x07,
      0x09,
  };
  uint8_t actual[sizeof(Payload)];
  memcpy(actual, &p, sizeof(p));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, sizeof(expected));
}

/** A known AckPayload serialises to the expected little endian bytes. */
void test_ack_byte_image() {
  AckPayload a{};
  countingUid(a.uid);
  a.seq = 42;

  const uint8_t expected[20] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
      0x2A, 0x00, 0x00, 0x00,
  };
  uint8_t actual[sizeof(AckPayload)];
  memcpy(actual, &a, sizeof(a));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, sizeof(expected));
}

/** Bytes received off the air read back as the same Payload. */
void test_payload_round_trips() {
  Payload sent{};
  countingUid(sent.uid);
  sent.seq = 99;
  sent.kwh_import = 100.125f;
  sent.voltage = 239.5f;
  uint8_t air[sizeof(Payload)];
  memcpy(air, &sent, sizeof(sent));

  Payload received;
  memcpy(&received, air, sizeof(received));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(sent.uid, received.uid, UID_LEN);
  TEST_ASSERT_EQUAL_UINT32(99, received.seq);
  TEST_ASSERT_EQUAL_FLOAT(100.125f, received.kwh_import);
  TEST_ASSERT_EQUAL_FLOAT(239.5f, received.voltage);
}

/** uidToHex() gives 32 lowercase hex digits and returns its buffer. */
void test_uid_to_hex() {
  uint8_t uid[UID_LEN];
  countingUid(uid);
  uid[15] = 0xAF;
  char out[UID_HEX_LEN];
  const char *returned = uidToHex(uid, out);
  TEST_ASSERT_EQUAL_PTR(out, returned);
  TEST_ASSERT_EQUAL_STRING("000102030405060708090a0b0c0d0eaf", out);
}

/** uidEquals() is true for the same UID and false when any byte differs. */
void test_uid_equals() {
  uint8_t a[UID_LEN];
  uint8_t b[UID_LEN];
  countingUid(a);
  countingUid(b);
  TEST_ASSERT_TRUE(uidEquals(a, b));
  b[UID_LEN - 1] ^= 0x01;
  TEST_ASSERT_FALSE(uidEquals(a, b));
}

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_sizes);
  RUN_TEST(test_payload_field_offsets);
  RUN_TEST(test_ack_field_offsets);
  RUN_TEST(test_packets_fit_both_links);
  RUN_TEST(test_payload_byte_image);
  RUN_TEST(test_ack_byte_image);
  RUN_TEST(test_payload_round_trips);
  RUN_TEST(test_uid_to_hex);
  RUN_TEST(test_uid_equals);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
