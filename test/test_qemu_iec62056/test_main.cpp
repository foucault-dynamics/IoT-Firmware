/**
 * @file
 * Unit and driver tests for Iec6205621Reader, run in QEMU.
 *
 * Reaches the private baud rate table, OBIS parser and handshake through the
 * Iec6205621ReaderTest friend class, and runs whole sessions over a scripted
 * FakeBus.
 *
 * test_obis_rejects_non_numeric_value and
 * test_obis_missing_bracket_does_not_borrow_next_line guard against two fixed
 * parser bugs.
 */

#include <Arduino.h>
#include <unity.h>

#include <vector>

#include "../support/fake_bus.h"
#include "iec62056_21.h"

/** Forwards to Iec6205621Reader's private helpers. */
class Iec6205621ReaderTest {
 public:
  /**
   * Calls Iec6205621Reader::baudRateFromId().
   *
   * @param[in]  code  Baud rate ID character.
   * @param[out] baud  Rate in baud.
   * @return What baudRateFromId() returned.
   */
  static bool baud(char code, uint32_t &baud) { return Iec6205621Reader::baudRateFromId(code, baud); }

  /**
   * Calls Iec6205621Reader::parseObisFloat().
   *
   * @param[in]  block  Data block to search.
   * @param[in]  code   OBIS code to look for.
   * @param[out] out    Parsed value.
   * @return What parseObisFloat() returned.
   */
  static bool obis(const char *block, const char *code, float &out) {
    return Iec6205621Reader::parseObisFloat(String(block), code, out);
  }

  /**
   * Calls Iec6205621Reader::handshake().
   *
   * @param[in]  r     Reader to call it on, already init()ed.
   * @param[out] baud  Negotiated rate.
   * @return What handshake() returned.
   */
  static int handshake(Iec6205621Reader &r, uint32_t &baud) { return r.handshake(baud); }
};

namespace {

/** Written to outputs first, to check a failed call leaves them alone. */
constexpr float SENTINEL = -999.0f;
/** Identification message offering baud rate ID '5', 9600 baud. */
const char *ID_9600 = "/EMH5EM211\r\n";
/** Data block holding the import and export readings. */
const char *DATA_BLOCK =
    "1-0:1.8.0(001234.567*kWh)\r\n"
    "1-0:2.8.0(000045.123*kWh)\r\n"
    "!\r\n";

/**
 * Checks @p code maps to @p expected baud.
 *
 * @param[in] code      Baud rate ID character.
 * @param[in] expected  Expected rate in baud.
 */
void assertBaud(char code, uint32_t expected) {
  uint32_t baud = 0;
  TEST_ASSERT_TRUE(Iec6205621ReaderTest::baud(code, baud));
  TEST_ASSERT_EQUAL_UINT32(expected, baud);
}

/**
 * Checks an OBIS code is not found, and the output left untouched.
 *
 * @param[in] block  Data block to search.
 * @param[in] code   OBIS code to look for.
 */
void assertObisMissing(const char *block, const char *code) {
  float out = SENTINEL;
  TEST_ASSERT_FALSE_MESSAGE(Iec6205621ReaderTest::obis(block, code, out), "parser accepted a value it should have rejected");
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, out);
}

/**
 * Runs the handshake on a fresh reader over @p bus.
 *
 * @param[in]  bus   Bus with the meter's replies queued.
 * @param[out] baud  Negotiated rate, preset to 0.
 * @return What handshake() returned.
 */
int runHandshake(FakeBus &bus, uint32_t &baud) {
  Iec6205621Reader reader(Iec62056Config{});
  reader.init(bus);
  baud = 0;
  return Iec6205621ReaderTest::handshake(reader, baud);
}

/**
 * Checks the handshake fails with @p expected and never switches baud rate.
 *
 * @param[in] reply     Identification message the meter sends.
 * @param[in] expected  Expected handshake() result.
 */
void assertHandshakeFails(const char *reply, int expected) {
  FakeBus bus;
  bus.queueText(reply);
  uint32_t baud;
  TEST_ASSERT_EQUAL_INT(expected, runHandshake(bus, baud));
  TEST_ASSERT_EQUAL_UINT32(0, baud);
  TEST_ASSERT_EQUAL_size_t(0, bus.baudRates.size());
  // Only the request message, no ACK
  TEST_ASSERT_EQUAL_size_t(1, bus.sent.size());
}

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/** IDs '0' to '6' map to the standard's Table 6 rates. */
void test_baud_ids_map_to_table_6() {
  assertBaud('0', 300);
  assertBaud('1', 600);
  assertBaud('2', 1200);
  assertBaud('3', 2400);
  assertBaud('4', 4800);
  assertBaud('5', 9600);
  assertBaud('6', 19200);
}

/** Any other ID is rejected and the output left untouched. */
void test_unknown_baud_ids_rejected() {
  for (char code : {'7', '9', 'A', '/', '\0'}) {
    uint32_t baud = 12345;
    TEST_ASSERT_FALSE(Iec6205621ReaderTest::baud(code, baud));
    TEST_ASSERT_EQUAL_UINT32(12345, baud);
  }
}

/** A well formed OBIS line parses to its value. */
void test_obis_parses_value() {
  float out = SENTINEL;
  TEST_ASSERT_TRUE(Iec6205621ReaderTest::obis("1-0:1.8.0(001234.567*kWh)\r\n", "1-0:1.8.0", out));
  TEST_ASSERT_EQUAL_FLOAT(1234.567f, out);
}

/** Each code in a full data block reads its own line. */
void test_obis_picks_requested_code() {
  float out = SENTINEL;
  TEST_ASSERT_TRUE(Iec6205621ReaderTest::obis(DATA_BLOCK, "1-0:2.8.0", out));
  TEST_ASSERT_EQUAL_FLOAT(45.123f, out);
}

/** A similar code earlier in the block is not picked. */
void test_obis_skips_similar_code() {
  float out = SENTINEL;
  TEST_ASSERT_TRUE(Iec6205621ReaderTest::obis("1-0:1.8.1(000999.000*kWh)\r\n1-0:1.8.0(001234.567*kWh)\r\n",
                                              "1-0:1.8.0", out));
  TEST_ASSERT_EQUAL_FLOAT(1234.567f, out);
}

/** A code that is not in the block is not found. */
void test_obis_missing_code() {
  assertObisMissing("1-0:2.8.0(000045.123*kWh)\r\n", "1-0:1.8.0");
}

/** A code with no opening bracket is not parsed. */
void test_obis_missing_bracket() {
  assertObisMissing("1-0:1.8.0 001234.567*kWh\r\n", "1-0:1.8.0");
}

/** A value with no unit still parses, the unit is optional. */
void test_obis_missing_unit() {
  float out = SENTINEL;
  TEST_ASSERT_TRUE(Iec6205621ReaderTest::obis("1-0:1.8.0(001234.567)\r\n", "1-0:1.8.0", out));
  TEST_ASSERT_EQUAL_FLOAT(1234.567f, out);
}

/**
 * Non numeric content in the brackets is rejected.
 *
 * Regression: String::toFloat() returned 0 for garbage, which was accepted.
 */
void test_obis_rejects_non_numeric_value() {
  assertObisMissing("1-0:1.8.0(abc*kWh)\r\n", "1-0:1.8.0");
}

/**
 * A code with no bracket on its own line does not take the next line's value.
 *
 * Regression: the search for '(' ran past the end of the line, so the import
 * reading below was read as 45.123.
 */
void test_obis_missing_bracket_does_not_borrow_next_line() {
  assertObisMissing("1-0:1.8.0\r\n1-0:2.8.0(000045.123*kWh)\r\n", "1-0:1.8.0");
}

/** A full session reads import and export, and switches to the offered rate. */
void test_session_reads_import_and_export() {
  FakeBus bus;
  bus.queueText(ID_9600);
  bus.queueText(DATA_BLOCK);
  Iec6205621Reader reader(Iec62056Config{});
  reader.init(bus);

  float import = SENTINEL;
  float exported = SENTINEL;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_import(&import));
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_export(&exported));
  TEST_ASSERT_EQUAL_FLOAT(1234.567f, import);
  TEST_ASSERT_EQUAL_FLOAT(45.123f, exported);

  TEST_ASSERT_EQUAL_size_t(2, bus.sent.size());
  const uint8_t request[] = {'/', '?', '!', '\r', '\n'};
  const uint8_t ack[] = {0x06, '0', '5', '0', '\r', '\n'};
  TEST_ASSERT_EQUAL_size_t(sizeof(request), bus.sent[0].size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(request, bus.sent[0].data(), sizeof(request));
  TEST_ASSERT_EQUAL_size_t(sizeof(ack), bus.sent[1].size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(ack, bus.sent[1].data(), sizeof(ack));

  TEST_ASSERT_EQUAL_size_t(1, bus.baudRates.size());
  TEST_ASSERT_EQUAL_UINT32(9600, bus.baudRates[0]);
}

/** An identification message split across reads is still assembled. */
void test_split_identification_is_joined() {
  FakeBus bus;
  bus.queueChunks({{{'/', 'E', 'M'}, 0}, {{'H', '5', 'E', 'M', '2', '1', '1', '\r', '\n'}, 5000}});
  uint32_t baud;
  TEST_ASSERT_EQUAL_INT(0, runHandshake(bus, baud));
  TEST_ASSERT_EQUAL_UINT32(9600, baud);
}

/** get_export() fails until a get_import() has succeeded. */
void test_export_needs_import_first() {
  FakeBus bus;
  Iec6205621Reader reader(Iec62056Config{});
  reader.init(bus);
  float exported = SENTINEL;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_export(&exported));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, exported);
}

/** A data block missing an OBIS code fails the read. */
void test_session_fails_on_missing_code() {
  FakeBus bus;
  bus.queueText(ID_9600);
  bus.queueText("1-0:1.8.0(001234.567*kWh)\r\n!\r\n");
  Iec6205621Reader reader(Iec62056Config{});
  reader.init(bus);
  float import = SENTINEL;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_import(&import));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, import);
}

/** A meter that never answers the request message fails with -1. */
void test_handshake_no_reply() {
  FakeBus bus;
  bus.queueSilence();
  uint32_t baud;
  TEST_ASSERT_EQUAL_INT(-1, runHandshake(bus, baud));
  TEST_ASSERT_EQUAL_size_t(0, bus.baudRates.size());
}

/** A reply that is not an identification message fails with -2. */
void test_handshake_malformed_id() {
  assertHandshakeFails("XYZ12345\r\n", -2);
  assertHandshakeFails("/EM\r\n", -2);
}

/** An identification message with an unknown baud rate ID fails with -3. */
void test_handshake_unknown_baud_id() {
  assertHandshakeFails("/EMH9EM211\r\n", -3);
}

/** IEC 62056-21 has no voltage reading here, so get_voltage() always fails. */
void test_voltage_unsupported() {
  FakeBus bus;
  Iec6205621Reader reader(Iec62056Config{});
  reader.init(bus);
  float voltage = SENTINEL;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_voltage(&voltage));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, voltage);
}

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_baud_ids_map_to_table_6);
  RUN_TEST(test_unknown_baud_ids_rejected);
  RUN_TEST(test_obis_parses_value);
  RUN_TEST(test_obis_picks_requested_code);
  RUN_TEST(test_obis_skips_similar_code);
  RUN_TEST(test_obis_missing_code);
  RUN_TEST(test_obis_missing_bracket);
  RUN_TEST(test_obis_missing_unit);
  RUN_TEST(test_obis_rejects_non_numeric_value);
  RUN_TEST(test_obis_missing_bracket_does_not_borrow_next_line);
  RUN_TEST(test_session_reads_import_and_export);
  RUN_TEST(test_split_identification_is_joined);
  RUN_TEST(test_export_needs_import_first);
  RUN_TEST(test_session_fails_on_missing_code);
  RUN_TEST(test_handshake_no_reply);
  RUN_TEST(test_handshake_malformed_id);
  RUN_TEST(test_handshake_unknown_baud_id);
  RUN_TEST(test_voltage_unsupported);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
