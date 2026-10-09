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

#include <vector>

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

  /**
   * Calls DlmsCosemReader::buildAarq().
   *
   * @param[in]  r    Reader to call it on.
   * @param[out] out  At least DLMS_APDU_MAX bytes.
   * @return Length of the AARQ.
   */
  static size_t aarq(DlmsCosemReader &r, uint8_t *out) { return r.buildAarq(out); }

  /**
   * Calls DlmsCosemReader::checkAare().
   *
   * @param[in] r     Reader to call it on.
   * @param[in] apdu  AARE APDU, without LLC.
   * @return What checkAare() returned.
   */
  static int checkAare(DlmsCosemReader &r, const std::vector<uint8_t> &apdu) {
    return r.checkAare(apdu.data(), apdu.size());
  }

  /**
   * Calls DlmsCosemReader::buildGetRequest().
   *
   * @param[in]  r          Reader to call it on.
   * @param[in]  obis       OBIS code, 6 bytes.
   * @param[in]  attribute  Attribute id.
   * @param[out] out        At least DLMS_GET_REQUEST_LEN bytes.
   */
  static void getRequest(DlmsCosemReader &r, const uint8_t *obis, uint8_t attribute, uint8_t *out) {
    r.buildGetRequest(obis, attribute, out);
  }

  /**
   * Calls DlmsCosemReader::parseGetResponse().
   *
   * @param[in]  r        Reader to call it on.
   * @param[in]  apdu     GET response APDU, without LLC.
   * @param[out] data     Points into @p apdu at the Data.
   * @param[out] dataLen  Length of the Data.
   * @return What parseGetResponse() returned.
   */
  static int getResponse(DlmsCosemReader &r, const std::vector<uint8_t> &apdu,
                         const uint8_t **data, size_t *dataLen) {
    return r.parseGetResponse(apdu.data(), apdu.size(), data, dataLen);
  }

  /**
   * Calls DlmsCosemReader::decodeNumber().
   *
   * @param[in]  r     Reader to call it on.
   * @param[in]  data  Data, starting at its type tag.
   * @param[out] val   Decoded value.
   * @return What decodeNumber() returned.
   */
  static int number(DlmsCosemReader &r, const std::vector<uint8_t> &data, double *val) {
    return r.decodeNumber(data.data(), data.size(), val);
  }

  /**
   * Calls DlmsCosemReader::decodeScalerUnit().
   *
   * @param[in]  r       Reader to call it on.
   * @param[in]  data    Data, starting at its type tag.
   * @param[in]  unit    Expected unit.
   * @param[out] scaler  Decoded scaler.
   * @return What decodeScalerUnit() returned.
   */
  static int scalerUnit(DlmsCosemReader &r, const std::vector<uint8_t> &data, uint8_t unit,
                        int8_t *scaler) {
    return r.decodeScalerUnit(data.data(), data.size(), unit, scaler);
  }
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

namespace {

/**
 * Returns the accepted AARE APDU from AARE_INFO, without its LLC header.
 *
 * @return The APDU.
 */
std::vector<uint8_t> aareApdu() {
  return std::vector<uint8_t>(AARE_INFO + 3, AARE_INFO + sizeof(AARE_INFO));
}

/**
 * Runs checkAare() on an APDU.
 *
 * @param[in] apdu  AARE APDU, without LLC.
 * @return What checkAare() returned.
 */
int checkAare(const std::vector<uint8_t> &apdu) {
  DlmsCosemReader reader(makeConfig(0, 1));
  return DlmsCosemReaderTest::checkAare(reader, apdu);
}

}  // namespace

/** The AARQ is LN, no ciphering, GET only, and a 125 byte receive PDU. */
void test_aarq_bytes() {
  const uint8_t expected[] = {
      0x60, 0x1D, 0xA1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08,
      0x01, 0x01, 0xBE, 0x10, 0x04, 0x0E, 0x01, 0x00, 0x00, 0x00, 0x06,
      0x5F, 0x1F, 0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x7D};
  DlmsCosemReader reader(makeConfig(0, 1));
  uint8_t out[DLMS_APDU_MAX];
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), DlmsCosemReaderTest::aarq(reader, out));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));
}

/** An AARE with result 0 and an InitiateResponse is accepted. */
void test_aare_accepted() {
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, checkAare(aareApdu()));
}

/** An outer length in long form, 81 29, is accepted. */
void test_aare_accepts_long_form_length() {
  std::vector<uint8_t> apdu = aareApdu();
  apdu.insert(apdu.begin() + 1, 0x81);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, checkAare(apdu));
}

/** Result 1, rejected permanently, fails. */
void test_aare_rejected() {
  std::vector<uint8_t> apdu = aareApdu();
  apdu[17] = 0x01;
  apdu[24] = 0x02;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, checkAare(apdu));
}

/** Result 0 with a ConfirmedServiceError in place of the InitiateResponse fails. */
void test_aare_rejects_service_error() {
  std::vector<uint8_t> apdu = aareApdu();
  apdu[29] = 0x0E;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, checkAare(apdu));
}

/** An AARQ tag where the AARE should be fails. */
void test_aare_rejects_wrong_tag() {
  std::vector<uint8_t> apdu = aareApdu();
  apdu[0] = 0x60;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, checkAare(apdu));
}

/** An AARE cut short fails instead of being read past its end. */
void test_aare_rejects_truncated() {
  std::vector<uint8_t> apdu = aareApdu();
  apdu.resize(20);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, checkAare(apdu));
}

namespace {

/** OBIS 1.0.1.8.0.255, imported active energy. */
const uint8_t IMPORT_OBIS[6] = {1, 0, 1, 8, 0, 255};

/**
 * Builds a GET request and checks it against its expected bytes.
 *
 * @param[in] obis       OBIS code, 6 bytes.
 * @param[in] attribute  Attribute id.
 * @param[in] expected   Expected APDU, DLMS_GET_REQUEST_LEN bytes.
 */
void assertGetRequest(const uint8_t *obis, uint8_t attribute, const uint8_t *expected) {
  DlmsCosemReader reader(makeConfig(0, 1));
  uint8_t out[DLMS_GET_REQUEST_LEN];
  DlmsCosemReaderTest::getRequest(reader, obis, attribute, out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, DLMS_GET_REQUEST_LEN);
}

}  // namespace

/** GET of the import value matches trace frame 7. */
void test_get_import_value() {
  const uint8_t expected[] = {0xC0, 0x01, 0xC1, 0x00, 0x03, 0x01, 0x00,
                              0x01, 0x08, 0x00, 0xFF, 0x02, 0x00};
  assertGetRequest(IMPORT_OBIS, DLMS_ATTR_VALUE, expected);
}

/** GET of the import scaler_unit matches trace frame 5. */
void test_get_import_scaler_unit() {
  const uint8_t expected[] = {0xC0, 0x01, 0xC1, 0x00, 0x03, 0x01, 0x00,
                              0x01, 0x08, 0x00, 0xFF, 0x03, 0x00};
  assertGetRequest(IMPORT_OBIS, DLMS_ATTR_SCALER_UNIT, expected);
}

/** GET of the voltage value carries OBIS 1.0.32.7.0.255. */
void test_get_voltage_value() {
  const uint8_t obis[6] = {1, 0, 32, 7, 0, 255};
  const uint8_t expected[] = {0xC0, 0x01, 0xC1, 0x00, 0x03, 0x01, 0x00,
                              0x20, 0x07, 0x00, 0xFF, 0x02, 0x00};
  assertGetRequest(obis, DLMS_ATTR_VALUE, expected);
}

namespace {

/**
 * Decodes a Data item and checks it succeeds with the expected value.
 *
 * @param[in] data      Data, starting at its type tag.
 * @param[in] expected  Value it must decode to exactly.
 */
void assertNumber(const std::vector<uint8_t> &data, double expected) {
  DlmsCosemReader reader(makeConfig(0, 1));
  double val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DlmsCosemReaderTest::number(reader, data, &val));
  char msg[64];
  snprintf(msg, sizeof(msg), "expected %f, got %f", expected, val);
  TEST_ASSERT_TRUE_MESSAGE(val == expected, msg);
}

/**
 * Decodes a Data item that must be rejected.
 *
 * @param[in] data  Data, starting at its type tag.
 * @return What decodeNumber() returned.
 */
int decodeNumber(const std::vector<uint8_t> &data) {
  DlmsCosemReader reader(makeConfig(0, 1));
  double val = 0;
  return DlmsCosemReaderTest::number(reader, data, &val);
}

/**
 * Parses a GET response, discarding the Data pointer.
 *
 * @param[in] apdu  GET response APDU, without LLC.
 * @return What parseGetResponse() returned.
 */
int parseGetResponse(const std::vector<uint8_t> &apdu) {
  DlmsCosemReader reader(makeConfig(0, 1));
  const uint8_t *data = nullptr;
  size_t dataLen = 0;
  return DlmsCosemReaderTest::getResponse(reader, apdu, &data, &dataLen);
}

}  // namespace

/** Tag 11, unsigned, decodes 0xFF as 255. */
void test_number_unsigned() { assertNumber({0x11, 0xFF}, 255); }

/** Tag 0F, integer, decodes 0xFF as -1. */
void test_number_integer() { assertNumber({0x0F, 0xFF}, -1); }

/** Tag 12, long-unsigned, decodes 0x8000 as 32768. */
void test_number_long_unsigned() { assertNumber({0x12, 0x80, 0x00}, 32768); }

/** Tag 10, long, decodes 0x8000 as -32768. */
void test_number_long() { assertNumber({0x10, 0x80, 0x00}, -32768); }

/** Tag 06, double-long-unsigned, decodes the trace's 12345678. */
void test_number_double_long_unsigned() { assertNumber({0x06, 0x00, 0xBC, 0x61, 0x4E}, 12345678); }

/** Tag 06 keeps a value above float's 2^24 exact. */
void test_number_double_long_unsigned_max() {
  assertNumber({0x06, 0xFF, 0xFF, 0xFF, 0xFF}, 4294967295.0);
}

/** Tag 05, double-long, decodes 0xFFFFFFFE as -2. */
void test_number_double_long() { assertNumber({0x05, 0xFF, 0xFF, 0xFF, 0xFE}, -2); }

/** Tag 15, long64-unsigned, decodes 2^32. */
void test_number_long64_unsigned() {
  assertNumber({0x15, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 4294967296.0);
}

/** Tag 17, float32, decodes 0x43660000 as 230.0. */
void test_number_float32() { assertNumber({0x17, 0x43, 0x66, 0x00, 0x00}, 230.0); }

/** Tag 09, octet-string, is not a number. */
void test_number_rejects_octet_string() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, decodeNumber({0x09, 0x02, 0x00, 0x01}));
}

/** A tag 06 with only 2 of its 4 bytes fails. */
void test_number_rejects_truncated() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, decodeNumber({0x06, 0x00, 0xBC}));
}

/** Empty Data fails. */
void test_number_rejects_empty() { TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, decodeNumber({})); }

/** Trace frame 8 parses, and its Data starts at the 06 tag. */
void test_get_response_data() {
  const std::vector<uint8_t> apdu = {0xC4, 0x01, 0xC1, 0x00, 0x06, 0x00, 0xBC, 0x61, 0x4E};
  DlmsCosemReader reader(makeConfig(0, 1));
  const uint8_t *data = nullptr;
  size_t dataLen = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS,
                        DlmsCosemReaderTest::getResponse(reader, apdu, &data, &dataLen));
  TEST_ASSERT_EQUAL_PTR(apdu.data() + 4, data);
  TEST_ASSERT_EQUAL_UINT(5, dataLen);
}

/** Data-access-result 4, object undefined, fails. */
void test_get_response_rejects_access_result() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, parseGetResponse({0xC4, 0x01, 0xC1, 0x01, 0x04}));
}

/** A get-response-with-datablock fails, since block transfer is out of scope. */
void test_get_response_rejects_datablock() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE,
                        parseGetResponse({0xC4, 0x02, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x01}));
}

/** An invoke id other than the C1 we sent fails. */
void test_get_response_rejects_wrong_invoke_id() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, parseGetResponse({0xC4, 0x01, 0xC2, 0x00, 0x11, 0x01}));
}

/** A SET response tag where the GET response should be fails. */
void test_get_response_rejects_wrong_tag() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, parseGetResponse({0xC5, 0x01, 0xC1, 0x00}));
}

/** A response shorter than its header fails. */
void test_get_response_rejects_short() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, parseGetResponse({0xC4, 0x01, 0xC1}));
}

/** Choice 00 with no Data after it fails. */
void test_get_response_rejects_missing_data() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, parseGetResponse({0xC4, 0x01, 0xC1, 0x00}));
}

namespace {

/**
 * Decodes a scaler_unit expected to be valid and checks its scaler.
 *
 * @param[in] data      Data, starting at its type tag.
 * @param[in] unit      Expected unit.
 * @param[in] expected  Scaler it must decode to.
 */
void assertScaler(const std::vector<uint8_t> &data, uint8_t unit, int8_t expected) {
  DlmsCosemReader reader(makeConfig(0, 1));
  int8_t scaler = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DlmsCosemReaderTest::scalerUnit(reader, data, unit, &scaler));
  TEST_ASSERT_EQUAL_INT8(expected, scaler);
}

/**
 * Decodes a scaler_unit that must be rejected.
 *
 * @param[in] data  Data, starting at its type tag.
 * @param[in] unit  Expected unit.
 * @return What decodeScalerUnit() returned.
 */
int decodeScalerUnit(const std::vector<uint8_t> &data, uint8_t unit) {
  DlmsCosemReader reader(makeConfig(0, 1));
  int8_t scaler = 0;
  return DlmsCosemReaderTest::scalerUnit(reader, data, unit, &scaler);
}

}  // namespace

/** Trace frame 6, scaler 0 in Wh. */
void test_scaler_unit_zero() { assertScaler({0x02, 0x02, 0x0F, 0x00, 0x16, 0x1E}, DLMS_UNIT_WH, 0); }

/** Scaler 0xFF is -1, as for a voltage in tenths. */
void test_scaler_unit_negative() { assertScaler({0x02, 0x02, 0x0F, 0xFF, 0x16, 0x23}, DLMS_UNIT_V, -1); }

/** Scaler 3, as for an energy counted in kWh. */
void test_scaler_unit_positive() { assertScaler({0x02, 0x02, 0x0F, 0x03, 0x16, 0x1E}, DLMS_UNIT_WH, 3); }

/** A register in V read by an energy getter fails. */
void test_scaler_unit_rejects_wrong_unit() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, decodeScalerUnit({0x02, 0x02, 0x0F, 0x00, 0x16, 0x23}, DLMS_UNIT_WH));
}

/** A plain number where the structure should be fails. */
void test_scaler_unit_rejects_number() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, decodeScalerUnit({0x06, 0x00, 0xBC, 0x61, 0x4E}, DLMS_UNIT_WH));
}

/** A structure cut short fails instead of being read past its end. */
void test_scaler_unit_rejects_truncated() {
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, decodeScalerUnit({0x02, 0x02, 0x0F, 0x00, 0x16}, DLMS_UNIT_WH));
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
  RUN_TEST(test_aarq_bytes);
  RUN_TEST(test_aare_accepted);
  RUN_TEST(test_aare_accepts_long_form_length);
  RUN_TEST(test_aare_rejected);
  RUN_TEST(test_aare_rejects_service_error);
  RUN_TEST(test_aare_rejects_wrong_tag);
  RUN_TEST(test_aare_rejects_truncated);
  RUN_TEST(test_get_import_value);
  RUN_TEST(test_get_import_scaler_unit);
  RUN_TEST(test_get_voltage_value);
  RUN_TEST(test_number_unsigned);
  RUN_TEST(test_number_integer);
  RUN_TEST(test_number_long_unsigned);
  RUN_TEST(test_number_long);
  RUN_TEST(test_number_double_long_unsigned);
  RUN_TEST(test_number_double_long_unsigned_max);
  RUN_TEST(test_number_double_long);
  RUN_TEST(test_number_long64_unsigned);
  RUN_TEST(test_number_float32);
  RUN_TEST(test_number_rejects_octet_string);
  RUN_TEST(test_number_rejects_truncated);
  RUN_TEST(test_number_rejects_empty);
  RUN_TEST(test_get_response_data);
  RUN_TEST(test_get_response_rejects_access_result);
  RUN_TEST(test_get_response_rejects_datablock);
  RUN_TEST(test_get_response_rejects_wrong_invoke_id);
  RUN_TEST(test_get_response_rejects_wrong_tag);
  RUN_TEST(test_get_response_rejects_short);
  RUN_TEST(test_get_response_rejects_missing_data);
  RUN_TEST(test_scaler_unit_zero);
  RUN_TEST(test_scaler_unit_negative);
  RUN_TEST(test_scaler_unit_positive);
  RUN_TEST(test_scaler_unit_rejects_wrong_unit);
  RUN_TEST(test_scaler_unit_rejects_number);
  RUN_TEST(test_scaler_unit_rejects_truncated);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}