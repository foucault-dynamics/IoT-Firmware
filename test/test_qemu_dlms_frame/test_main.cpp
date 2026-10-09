/**
 * @file
 * Unit tests for DLMS/COSEM HDLC framing, run in QEMU.
 *
 * Reaches DlmsCosemReader's private state through the DlmsCosemReaderTest
 * friend class.
 */

#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include <unity.h>

#include "../support/fake_bus.h"
#include "dlms_cosem.h"
#include "hdlc.h"

/** Forwards to DlmsCosemReader's private state. */
class DlmsCosemReaderTest {
 public:
  /**
   * Reads DlmsCosemReader::client.
   *
   * @param[in] r  Reader to read it from.
   * @return The encoded client address init() built.
   */
  static HdlcAddress client(const DlmsCosemReader &r) { return r.client; }

  /**
   * Reads DlmsCosemReader::server.
   *
   * @param[in] r  Reader to read it from.
   * @return The encoded server address init() built.
   */
  static HdlcAddress server(const DlmsCosemReader &r) { return r.server; }
};

namespace {

/**
 * Checks the FCS of a buffer against its expected wire bytes.
 *
 * @param[in] data  Bytes to checksum.
 * @param[in] len   Number of bytes in @p data.
 * @param[in] lo    Expected first FCS byte on the wire.
 * @param[in] hi    Expected second FCS byte on the wire.
 */
void assertFcs(const uint8_t *data, size_t len, uint8_t lo, uint8_t hi) {
  uint16_t fcs = hdlcFcs(data, len);
  TEST_ASSERT_EQUAL_HEX8(lo, fcs & 0xFF);
  TEST_ASSERT_EQUAL_HEX8(hi, fcs >> 8);
}

/**
 * Encodes an address and checks it against its expected wire bytes.
 *
 * @param[in] upper     Upper HDLC address (logical device or client SAP).
 * @param[in] lower     Lower HDLC address (physical device).
 * @param[in] size      Address size in bytes.
 * @param[in] expected  Expected wire bytes, @p size long.
 */
void assertAddress(uint16_t upper, uint16_t lower, uint8_t size,
                   const uint8_t *expected) {
  HdlcAddress addr;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, hdlcAddress(upper, lower, size, &addr));
  TEST_ASSERT_EQUAL_UINT(size, addr.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, addr.bytes, size);
}

}  // namespace

/** The CRC-16/X.25 check value of "123456789" is 0x906E. */
void test_fcs_check_value() {
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x906E, hdlcFcs(data, sizeof(data)));
}

/** SNRM from client 16 to server 1 ends in 0F 01. */
void test_fcs_snrm() {
  const uint8_t header[] = {0xA0, 0x07, 0x03, 0x21, 0x93};
  assertFcs(header, sizeof(header), 0x0F, 0x01);
}

/** UA from server 1 to client 16 ends in 01 40. */
void test_fcs_ua() {
  const uint8_t header[] = {0xA0, 0x07, 0x21, 0x03, 0x73};
  assertFcs(header, sizeof(header), 0x01, 0x40);
}

/** Public client 16 is one byte, 21. */
void test_address_client_16() {
  const uint8_t expected[] = {0x21};
  assertAddress(16, 0, 1, expected);
}

/** Server logical device 1 with a one byte address is 03. */
void test_address_server_1() {
  const uint8_t expected[] = {0x03};
  assertAddress(1, 0, 1, expected);
}

/** Logical 1, physical 17 in two bytes is 02 23, end bit only on the last. */
void test_address_two_bytes() {
  const uint8_t expected[] = {0x02, 0x23};
  assertAddress(1, 17, 2, expected);
}

/** Logical 1, physical 0x1234 in four bytes splits each half into 7 bit groups. */
void test_address_four_bytes() {
  const uint8_t expected[] = {0x00, 0x02, 0x48, 0x69};
  assertAddress(1, 0x1234, 4, expected);
}

/** Three bytes is not a valid HDLC address size. */
void test_address_rejects_size_3() {
  HdlcAddress addr;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, hdlcAddress(1, 17, 3, &addr));
}

/** An address too big for its size is rejected instead of truncated. */
void test_address_rejects_overflow() {
  HdlcAddress addr;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, hdlcAddress(0x80, 0, 1, &addr));
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, hdlcAddress(1, 0x80, 2, &addr));
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, hdlcAddress(1, 0x4000, 4, &addr));
}

namespace {

/**
 * Builds a client 16 to server 1 frame and checks it against its wire bytes.
 *
 * @param[in] control      Control byte.
 * @param[in] info         Info field, nullptr when @p infoLen is 0.
 * @param[in] infoLen      Number of bytes in @p info.
 * @param[in] expected     Expected frame, flags included.
 * @param[in] expectedLen  Number of bytes in @p expected.
 */
void assertFrame(uint8_t control, const uint8_t *info, size_t infoLen,
                 const uint8_t *expected, size_t expectedLen) {
  HdlcAddress server;
  HdlcAddress client;
  hdlcAddress(1, 0, 1, &server);
  hdlcAddress(16, 0, 1, &client);
  uint8_t frame[HDLC_FRAME_MAX];
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, hdlcBuildFrame(&server, &client, control,
                                                     info, infoLen, frame, &len));
  TEST_ASSERT_EQUAL_UINT(expectedLen, len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, expectedLen);
}

}  // namespace

/** SNRM has no info field, so no HCS. */
void test_frame_snrm() {
  const uint8_t expected[] = {0x7E, 0xA0, 0x07, 0x03, 0x21, 0x93, 0x0F, 0x01, 0x7E};
  assertFrame(0x93, nullptr, 0, expected, sizeof(expected));
}

/** DISC has the same shape as SNRM with control 53. */
void test_frame_disc() {
  const uint8_t expected[] = {0x7E, 0xA0, 0x07, 0x03, 0x21, 0x53, 0x03, 0xC7, 0x7E};
  assertFrame(0x53, nullptr, 0, expected, sizeof(expected));
}

/** The AARQ I-frame carries LLC plus AARQ, with HCS FB AF and FCS E9 02. */
void test_frame_aarq() {
  const uint8_t info[] = {
      0xE6, 0xE6, 0x00, 0x60, 0x1D, 0xA1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74,
      0x05, 0x08, 0x01, 0x01, 0xBE, 0x10, 0x04, 0x0E, 0x01, 0x00, 0x00, 0x00,
      0x06, 0x5F, 0x1F, 0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80};
  uint8_t expected[45] = {0x7E, 0xA0, 0x2B, 0x03, 0x21, 0x10, 0xFB, 0xAF};
  memcpy(expected + 8, info, sizeof(info));
  expected[42] = 0xE9;
  expected[43] = 0x02;
  expected[44] = 0x7E;
  assertFrame(0x10, info, sizeof(info), expected, sizeof(expected));
}

/** An info field over HDLC_INFO_MAX is rejected. */
void test_frame_rejects_oversized_info() {
  HdlcAddress server;
  HdlcAddress client;
  hdlcAddress(1, 0, 1, &server);
  hdlcAddress(16, 0, 1, &client);
  uint8_t info[HDLC_INFO_MAX + 1] = {};
  uint8_t frame[HDLC_FRAME_MAX];
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, hdlcBuildFrame(&server, &client, 0x10, info,
                                                     sizeof(info), frame, &len));
}

namespace {

/** UA from server 1 to client 16, from the trace. */
const uint8_t UA[] = {0x7E, 0xA0, 0x07, 0x21, 0x03, 0x73, 0x01, 0x40, 0x7E};

/** Info field of an accepted AARE: LLC, then AARE with result 0. */
const uint8_t AARE_INFO[] = {
    0xE6, 0xE7, 0x00, 0x61, 0x29, 0xA1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74,
    0x05, 0x08, 0x01, 0x01, 0xA2, 0x03, 0x02, 0x01, 0x00, 0xA3, 0x05, 0xA1,
    0x03, 0x02, 0x01, 0x00, 0xBE, 0x10, 0x04, 0x0E, 0x08, 0x00, 0x06, 0x5F,
    0x1F, 0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x07};

/** AARE I-frame from server 1 to client 16, control 30, HCS 6C 7C. */
const uint8_t AARE[] = {
    0x7E, 0xA0, 0x37, 0x21, 0x03, 0x30, 0x6C, 0x7C, 0xE6, 0xE7, 0x00, 0x61,
    0x29, 0xA1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08, 0x01, 0x01,
    0xA2, 0x03, 0x02, 0x01, 0x00, 0xA3, 0x05, 0xA1, 0x03, 0x02, 0x01, 0x00,
    0xBE, 0x10, 0x04, 0x0E, 0x08, 0x00, 0x06, 0x5F, 0x1F, 0x04, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x80, 0x00, 0x07, 0x86, 0xDF, 0x7E};

/**
 * Rewrites a frame's FCS so that only the check under test fails.
 *
 * @param[in,out] frame  Frame bytes, flags included.
 * @param[in]     len    Number of bytes in @p frame.
 */
void refreshFcs(uint8_t *frame, size_t len) {
  uint16_t fcs = hdlcFcs(frame + 1, len - 4);
  frame[len - 3] = fcs & 0xFF;
  frame[len - 2] = fcs >> 8;
}

/**
 * Checks that a frame is rejected.
 *
 * @param[in] frame  Frame bytes, flags included.
 * @param[in] len    Number of bytes in @p frame.
 */
void assertRejected(const uint8_t *frame, size_t len) {
  HdlcFrame parsed;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, hdlcParseFrame(frame, len, &parsed));
}

}  // namespace

/** UA parses to its addresses and control, with no info field. */
void test_parse_ua() {
  HdlcFrame parsed;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, hdlcParseFrame(UA, sizeof(UA), &parsed));
  TEST_ASSERT_EQUAL_UINT(1, parsed.dest.len);
  TEST_ASSERT_EQUAL_HEX8(0x21, parsed.dest.bytes[0]);
  TEST_ASSERT_EQUAL_UINT(1, parsed.src.len);
  TEST_ASSERT_EQUAL_HEX8(0x03, parsed.src.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x73, parsed.control);
  TEST_ASSERT_NULL(parsed.info);
  TEST_ASSERT_EQUAL_UINT(0, parsed.infoLen);
}

/** AARE parses with its info field extracted exactly. */
void test_parse_aare() {
  HdlcFrame parsed;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, hdlcParseFrame(AARE, sizeof(AARE), &parsed));
  TEST_ASSERT_EQUAL_HEX8(0x30, parsed.control);
  TEST_ASSERT_EQUAL_UINT(sizeof(AARE_INFO), parsed.infoLen);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(AARE_INFO, parsed.info, sizeof(AARE_INFO));
}

/** A corrupted FCS is rejected. */
void test_parse_rejects_bad_fcs() {
  uint8_t frame[sizeof(UA)];
  memcpy(frame, UA, sizeof(UA));
  frame[7] ^= 0xFF;
  assertRejected(frame, sizeof(frame));
}

/** A corrupted HCS is rejected even when the FCS matches. */
void test_parse_rejects_bad_hcs() {
  uint8_t frame[sizeof(AARE)];
  memcpy(frame, AARE, sizeof(AARE));
  frame[6] ^= 0xFF;
  refreshFcs(frame, sizeof(frame));
  assertRejected(frame, sizeof(frame));
}

/** A length field that does not match the frame is rejected. */
void test_parse_rejects_wrong_length() {
  uint8_t frame[sizeof(UA)];
  memcpy(frame, UA, sizeof(UA));
  frame[2] = 0x08;
  refreshFcs(frame, sizeof(frame));
  assertRejected(frame, sizeof(frame));
}

/** A format type other than A is rejected. */
void test_parse_rejects_wrong_format() {
  uint8_t frame[sizeof(UA)];
  memcpy(frame, UA, sizeof(UA));
  frame[1] = 0xB0;
  refreshFcs(frame, sizeof(frame));
  assertRejected(frame, sizeof(frame));
}

/** An address with no end bit within 4 bytes is rejected. */
void test_parse_rejects_unterminated_address() {
  uint8_t frame[] = {0x7E, 0xA0, 0x0A, 0x02, 0x02, 0x02, 0x02, 0x02, 0x73, 0x00, 0x00, 0x7E};
  refreshFcs(frame, sizeof(frame));
  assertRejected(frame, sizeof(frame));
}

namespace {

/**
 * Builds a config for public client 16 and server logical device 1.
 *
 * @param[in] physical  Server physical address.
 * @param[in] size      Server address size in bytes.
 * @return The config.
 */
DlmsCosemConfig makeConfig(uint16_t physical, uint8_t size) {
  DlmsCosemConfig config = {};
  config.clientSap = 16;
  config.serverLogical = 1;
  config.serverPhysical = physical;
  config.serverAddrLen = size;
  return config;
}

/**
 * Checks an encoded address against its expected wire bytes.
 *
 * @param[in] expected  Expected wire bytes.
 * @param[in] len       Number of bytes in @p expected.
 * @param[in] actual    Address to check.
 */
void assertEncoded(const uint8_t *expected, size_t len, const HdlcAddress &actual) {
  TEST_ASSERT_EQUAL_UINT(len, actual.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual.bytes, len);
}

}  // namespace

/** init() encodes client 16 as 21 and a one byte server 1 as 03. */
void test_init_one_byte_server() {
  DlmsCosemReader reader(makeConfig(0, 1));
  FakeBus bus;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.init(bus));
  const uint8_t client[] = {0x21};
  const uint8_t server[] = {0x03};
  assertEncoded(client, sizeof(client), DlmsCosemReaderTest::client(reader));
  assertEncoded(server, sizeof(server), DlmsCosemReaderTest::server(reader));
}

/** init() encodes a four byte server address with its physical part. */
void test_init_four_byte_server() {
  DlmsCosemReader reader(makeConfig(0x1234, 4));
  FakeBus bus;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.init(bus));
  const uint8_t server[] = {0x00, 0x02, 0x48, 0x69};
  assertEncoded(server, sizeof(server), DlmsCosemReaderTest::server(reader));
}

/** init() rejects a server address size of 3. */
void test_init_rejects_size_3() {
  DlmsCosemReader reader(makeConfig(17, 3));
  FakeBus bus;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.init(bus));
}

/** init() rejects a client SAP that does not fit in one byte. */
void test_init_rejects_large_client() {
  DlmsCosemConfig config = makeConfig(0, 1);
  config.clientSap = 0x80;
  DlmsCosemReader reader(config);
  FakeBus bus;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.init(bus));
}

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_fcs_check_value);
  RUN_TEST(test_fcs_snrm);
  RUN_TEST(test_fcs_ua);
  RUN_TEST(test_address_client_16);
  RUN_TEST(test_address_server_1);
  RUN_TEST(test_address_two_bytes);
  RUN_TEST(test_address_four_bytes);
  RUN_TEST(test_address_rejects_size_3);
  RUN_TEST(test_address_rejects_overflow);
  RUN_TEST(test_frame_snrm);
  RUN_TEST(test_frame_disc);
  RUN_TEST(test_frame_aarq);
  RUN_TEST(test_frame_rejects_oversized_info);
  RUN_TEST(test_parse_ua);
  RUN_TEST(test_parse_aare);
  RUN_TEST(test_parse_rejects_bad_fcs);
  RUN_TEST(test_parse_rejects_bad_hcs);
  RUN_TEST(test_parse_rejects_wrong_length);
  RUN_TEST(test_parse_rejects_wrong_format);
  RUN_TEST(test_parse_rejects_unterminated_address);
  RUN_TEST(test_init_one_byte_server);
  RUN_TEST(test_init_four_byte_server);
  RUN_TEST(test_init_rejects_size_3);
  RUN_TEST(test_init_rejects_large_client);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}