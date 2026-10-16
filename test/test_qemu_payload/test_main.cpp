/**
 * @file
 * Tests for the over the air packet formats in shared_payload.h, run in QEMU.
 *
 * Pins the size, field offsets and little endian byte image of Payload and
 * AckPayload, so a change to either struct fails here before it reaches a
 * board that would reject the other boards' packets. Also covers the UID
 * helpers and the LinkFrame discovery frame with its make and parse helpers.
 */

#include <Arduino.h>
#include <esp_now.h>
#include <unity.h>

#include <cstddef>
#include <cstring>

#include "shared_payload.h"

static_assert(sizeof(Payload) == 35, "Payload is 35 bytes on the air");
static_assert(sizeof(AckPayload) == 20, "AckPayload is 20 bytes on the air");
static_assert(sizeof(Payload) <= ESP_NOW_MAX_DATA_LEN, "Payload fits one ESP-NOW frame");
static_assert(sizeof(LinkFrame) == 23, "LinkFrame is 23 bytes on the air");
static_assert(sizeof(LinkFrame) != sizeof(Payload), "ESP-NOW tells LinkFrame and Payload apart by size");

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

/**
 * @defgroup test_qemu_payload Radio packet formats
 * @ingroup tests
 * Tests in test_qemu_payload/test_main.cpp.
 * @{
 */

/** Payload is 35 bytes and AckPayload 20, with no padding. */
void test_sizes() {
  TEST_ASSERT_EQUAL_size_t(35, sizeof(Payload));
  TEST_ASSERT_EQUAL_size_t(20, sizeof(AckPayload));
}

/** Payload's fields sit at fixed offsets: uid, seq, import, export, voltage, community, unit, node type. */
void test_payload_field_offsets() {
  TEST_ASSERT_EQUAL_size_t(0, offsetof(Payload, uid));
  TEST_ASSERT_EQUAL_size_t(16, offsetof(Payload, seq));
  TEST_ASSERT_EQUAL_size_t(20, offsetof(Payload, kwh_import));
  TEST_ASSERT_EQUAL_size_t(24, offsetof(Payload, kwh_export));
  TEST_ASSERT_EQUAL_size_t(28, offsetof(Payload, voltage));
  TEST_ASSERT_EQUAL_size_t(32, offsetof(Payload, community_id));
  TEST_ASSERT_EQUAL_size_t(33, offsetof(Payload, unit_id));
  TEST_ASSERT_EQUAL_size_t(34, offsetof(Payload, node_type));
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
  p.node_type = static_cast<uint8_t>(NodeType::Ir);

  const uint8_t expected[35] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
      0x04, 0x03, 0x02, 0x01,
      0x00, 0x50, 0x9A, 0x44,
      0x00, 0x00, 0x35, 0x42,
      0x00, 0x00, 0x66, 0x43,
      0x07,
      0x09,
      0x02,
  };
  uint8_t actual[sizeof(Payload)];
  memcpy(actual, &p, sizeof(p));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, sizeof(expected));
}

/** NodeType's values are the on air encoding: RS485 0, CV 1, IR 2. */
void test_node_type_values() {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(NodeType::Rs485));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(NodeType::Cv));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(NodeType::Ir));
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

/** LinkFrame's fields sit at fixed offsets: magic, type, epoch, uid. */
void test_link_frame_field_offsets() {
  TEST_ASSERT_EQUAL_size_t(0, offsetof(LinkFrame, magic));
  TEST_ASSERT_EQUAL_size_t(2, offsetof(LinkFrame, type));
  TEST_ASSERT_EQUAL_size_t(3, offsetof(LinkFrame, epoch));
  TEST_ASSERT_EQUAL_size_t(7, offsetof(LinkFrame, uid));
}

/** linkFrameMake() serialises to the expected little endian bytes. */
void test_link_frame_byte_image() {
  uint8_t uid[UID_LEN];
  countingUid(uid);
  LinkFrame f = linkFrameMake(LinkFrameType::Join, 0x0A0B0C0D, uid);

  const uint8_t expected[23] = {
      'K', 'Z',
      0x02,
      0x0D, 0x0C, 0x0B, 0x0A,
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  };
  uint8_t actual[sizeof(LinkFrame)];
  memcpy(actual, &f, sizeof(f));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, sizeof(expected));
}

/** A beacon made with no UID carries an all zero UID. */
void test_link_frame_null_uid_is_zero() {
  LinkFrame f = linkFrameMake(LinkFrameType::Beacon, 1, nullptr);
  const uint8_t zero[UID_LEN] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, f.uid, UID_LEN);
}

/** A made frame parses back to the same fields. */
void test_link_frame_parse_accepts_made_frame() {
  uint8_t uid[UID_LEN];
  countingUid(uid);
  LinkFrame sent = linkFrameMake(LinkFrameType::JoinAck, 77, uid);
  uint8_t air[sizeof(LinkFrame)];
  memcpy(air, &sent, sizeof(sent));

  LinkFrame received{};
  TEST_ASSERT_TRUE(linkFrameParse(air, sizeof(air), received));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkFrameType::JoinAck), received.type);
  TEST_ASSERT_EQUAL_UINT32(77, received.epoch);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(uid, received.uid, UID_LEN);
}

/** Wrong length, wrong magic and unknown types are all rejected, leaving the output untouched. */
void test_link_frame_parse_rejects_bad_frames() {
  LinkFrame good = linkFrameMake(LinkFrameType::Beacon, 5, nullptr);
  uint8_t air[sizeof(Payload)] = {};
  memcpy(air, &good, sizeof(good));

  LinkFrame out{};
  out.epoch = 0xDEADBEEF;

  // A Payload sized frame is never a LinkFrame
  TEST_ASSERT_FALSE(linkFrameParse(air, sizeof(Payload), out));
  TEST_ASSERT_FALSE(linkFrameParse(air, sizeof(LinkFrame) - 1, out));

  air[0] = 'X';
  TEST_ASSERT_FALSE(linkFrameParse(air, sizeof(LinkFrame), out));
  air[0] = 'K';

  air[2] = 0;
  TEST_ASSERT_FALSE(linkFrameParse(air, sizeof(LinkFrame), out));
  air[2] = 4;
  TEST_ASSERT_FALSE(linkFrameParse(air, sizeof(LinkFrame), out));

  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, out.epoch);
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

/** @} */

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_sizes);
  RUN_TEST(test_payload_field_offsets);
  RUN_TEST(test_ack_field_offsets);
  RUN_TEST(test_packets_fit_both_links);
  RUN_TEST(test_payload_byte_image);
  RUN_TEST(test_node_type_values);
  RUN_TEST(test_ack_byte_image);
  RUN_TEST(test_payload_round_trips);
  RUN_TEST(test_link_frame_field_offsets);
  RUN_TEST(test_link_frame_byte_image);
  RUN_TEST(test_link_frame_null_uid_is_zero);
  RUN_TEST(test_link_frame_parse_accepts_made_frame);
  RUN_TEST(test_link_frame_parse_rejects_bad_frames);
  RUN_TEST(test_uid_to_hex);
  RUN_TEST(test_uid_equals);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
