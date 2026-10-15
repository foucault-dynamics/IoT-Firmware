/**
 * @file
 * Driver tests for ModbusRtuReader over a scripted FakeBus, run in QEMU.
 *
 * Covers decoding a valid response in both register formats, every way a
 * response is rejected, and bus timing: the 500 ms timeout, replies split
 * across reads, and recovery after a failed attempt. millis() and micros() run
 * in real time under QEMU, so the timeouts are real.
 */

#include <Arduino.h>
#include <unity.h>

#include <vector>

#include "../support/fake_bus.h"
#include "modbus_rtu.h"

/** Forwards to ModbusRtuReader's private helpers. */
class ModbusRtuReaderTest {
 public:
  /**
   * Calls ModbusRtuReader::modbus_crc().
   *
   * @param[in] r  Reader to call it on.
   * @param[in] d  Bytes to checksum.
   * @param[in] n  Number of bytes in @p d.
   * @return The CRC.
   */
  static uint16_t crc(ModbusRtuReader &r, const uint8_t *d, size_t n) { return r.modbus_crc(d, n); }
};

namespace {

/** Register pair the import reading is read from. */
constexpr uint16_t IMPORT_ADDRESS = 0x0004;
/** Written to outputs first, to check a failed read leaves them alone. */
constexpr float SENTINEL = -999.0f;

/**
 * Builds the config every test reads with: slave 1, function 0x03, 9600 8N1.
 *
 * @param[in] format  How register pairs are decoded.
 * @return The config.
 */
ModbusRtuConfig makeConfig(RegisterFormat format) {
  ModbusRtuConfig config = {};
  config.slaveAddress = 1;
  config.functionCode = 0x03;
  config.bus.baudRate = 9600;
  config.bus.format = SERIAL_8N1;
  config.registerFormat = format;
  config.import_address = IMPORT_ADDRESS;
  return config;
}

/**
 * Appends the Modbus CRC to a frame, low byte first.
 *
 * @param[in] frame  Frame without its CRC.
 * @return @p frame followed by its CRC.
 */
std::vector<uint8_t> withCrc(std::vector<uint8_t> frame) {
  ModbusRtuReader reader(makeConfig(RegisterFormat::IEEE_754Float));
  uint16_t crc = ModbusRtuReaderTest::crc(reader, frame.data(), frame.size());
  frame.push_back(crc & 0xFF);
  frame.push_back(crc >> 8);
  return frame;
}

/**
 * Builds a valid 9 byte response carrying one 32 bit value.
 *
 * @param[in] raw  Value, sent big endian, high register first.
 * @return The response, CRC included.
 */
std::vector<uint8_t> validResponse(uint32_t raw) {
  return withCrc({0x01, 0x03, 0x04,
                  static_cast<uint8_t>(raw >> 24), static_cast<uint8_t>(raw >> 16),
                  static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw)});
}

/**
 * Reads the import register through a reader set up on @p bus.
 *
 * @param[in]  bus     Bus with the meter's replies queued.
 * @param[in]  format  How the register pair is decoded.
 * @param[out] val     Decoded value, preset to SENTINEL.
 * @return What get_import() returned.
 */
int readImport(FakeBus &bus, RegisterFormat format, float &val) {
  ModbusRtuReader reader(makeConfig(format));
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.init(bus));
  val = SENTINEL;
  return reader.get_import(&val);
}

/**
 * Checks a response is rejected and the output left untouched.
 *
 * @param[in] response  What the meter replies with.
 */
void assertRejected(const std::vector<uint8_t> &response) {
  FakeBus bus;
  bus.queueReply(response);
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, readImport(bus, RegisterFormat::IEEE_754Float, val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
}

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/**
 * @defgroup test_qemu_modbus_driver Modbus RTU driver
 * @ingroup tests
 * Tests in test_qemu_modbus_driver/test_main.cpp.
 * @{
 */

/** The request sent is a 2 register read of the import address. */
void test_sends_read_request_for_import() {
  FakeBus bus;
  bus.queueReply(validResponse(0));
  float val;
  readImport(bus, RegisterFormat::IEEE_754Float, val);

  const std::vector<uint8_t> expected = {0x01, 0x03, 0x00, 0x04, 0x00, 0x02, 0x85, 0xCA};
  TEST_ASSERT_EQUAL_size_t(1, bus.sent.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), bus.sent[0].data(), expected.size());
}

/** An IEEE 754 response decodes big endian, high register first. */
void test_decodes_ieee754_float() {
  FakeBus bus;
  // 230.5f is 0x43668000
  bus.queueReply(validResponse(0x43668000));
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, readImport(bus, RegisterFormat::IEEE_754Float, val));
  TEST_ASSERT_EQUAL_FLOAT(230.5f, val);
}

/** A scaled integer response is divided by 1000. */
void test_decodes_scaled_int() {
  FakeBus bus;
  bus.queueReply(validResponse(1234567));
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, readImport(bus, RegisterFormat::ScaledInt, val));
  TEST_ASSERT_EQUAL_FLOAT(1234.567f, val);
}

/** A response with a corrupted CRC is rejected. */
void test_rejects_bad_crc() {
  std::vector<uint8_t> response = validResponse(0x43668000);
  response.back() ^= 0xFF;
  assertRejected(response);
}

/** A frame shorter than 4 bytes is rejected. */
void test_rejects_frame_under_4_bytes() {
  assertRejected({0x01, 0x03, 0x04});
}

/** A well formed frame of 4 to 8 bytes that is not an exception is rejected. */
void test_rejects_short_frame() {
  assertRejected(withCrc({0x01, 0x03}));
  assertRejected(withCrc({0x01, 0x03, 0x04, 0x43, 0x66, 0x80}));
}

/** A response from another slave is rejected. */
void test_rejects_wrong_slave() {
  assertRejected(withCrc({0x02, 0x03, 0x04, 0x43, 0x66, 0x80, 0x00}));
}

/** An exception response, function code with the top bit set, is rejected. */
void test_rejects_exception_response() {
  // Exception 0x02, illegal data address
  assertRejected(withCrc({0x01, 0x83, 0x02}));
}

/** A response to a different function code is rejected. */
void test_rejects_wrong_function_code() {
  assertRejected(withCrc({0x01, 0x04, 0x04, 0x43, 0x66, 0x80, 0x00}));
}

/** A byte count other than 4 is rejected. */
void test_rejects_wrong_byte_count() {
  assertRejected(withCrc({0x01, 0x03, 0x02, 0x43, 0x66, 0x80, 0x00}));
}

/** A frame longer than 9 bytes is rejected. */
void test_rejects_frame_over_9_bytes() {
  assertRejected(withCrc({0x01, 0x03, 0x04, 0x43, 0x66, 0x80, 0x00, 0x00}));
}

/** A meter that never replies fails the read after the 500 ms timeout. */
void test_no_reply_times_out() {
  FakeBus bus;
  bus.queueSilence();
  float val;
  uint32_t start = millis();
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, readImport(bus, RegisterFormat::IEEE_754Float, val));
  uint32_t elapsed = millis() - start;
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(TIMEOUT, elapsed);
  TEST_ASSERT_LESS_THAN_UINT32(TIMEOUT + 200, elapsed);
}

/** A reply split across reads by a gap shorter than T3.5 is one frame. */
void test_split_reply_within_t35_is_joined() {
  std::vector<uint8_t> response = validResponse(0x43668000);
  FakeBus bus;
  // T3.5 at 9600 8N1 is 3646 us
  bus.queueChunks({{std::vector<uint8_t>(response.begin(), response.begin() + 4), 0},
                   {std::vector<uint8_t>(response.begin() + 4, response.end()), 1000}});
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, readImport(bus, RegisterFormat::IEEE_754Float, val));
  TEST_ASSERT_EQUAL_FLOAT(230.5f, val);
}

/** A gap longer than T3.5 ends the frame, so the split reply is rejected. */
void test_split_reply_past_t35_is_rejected() {
  std::vector<uint8_t> response = validResponse(0x43668000);
  FakeBus bus;
  bus.queueChunks({{std::vector<uint8_t>(response.begin(), response.begin() + 4), 0},
                   {std::vector<uint8_t>(response.begin() + 4, response.end()), 10000}});
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, readImport(bus, RegisterFormat::IEEE_754Float, val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
}

/** After a corrupted reply, the next read on the same reader succeeds. */
void test_second_attempt_succeeds_after_failure() {
  std::vector<uint8_t> corrupted = validResponse(0x43668000);
  corrupted.back() ^= 0xFF;
  FakeBus bus;
  bus.queueReply(corrupted);
  bus.queueReply(validResponse(0x43668000));

  ModbusRtuReader reader(makeConfig(RegisterFormat::IEEE_754Float));
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.init(bus));
  float val = SENTINEL;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_import(&val));
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_import(&val));
  TEST_ASSERT_EQUAL_FLOAT(230.5f, val);
  TEST_ASSERT_EQUAL_size_t(2, bus.sent.size());
}

/** @} */

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_sends_read_request_for_import);
  RUN_TEST(test_decodes_ieee754_float);
  RUN_TEST(test_decodes_scaled_int);
  RUN_TEST(test_rejects_bad_crc);
  RUN_TEST(test_rejects_frame_under_4_bytes);
  RUN_TEST(test_rejects_short_frame);
  RUN_TEST(test_rejects_wrong_slave);
  RUN_TEST(test_rejects_exception_response);
  RUN_TEST(test_rejects_wrong_function_code);
  RUN_TEST(test_rejects_wrong_byte_count);
  RUN_TEST(test_rejects_frame_over_9_bytes);
  RUN_TEST(test_no_reply_times_out);
  RUN_TEST(test_split_reply_within_t35_is_joined);
  RUN_TEST(test_split_reply_past_t35_is_rejected);
  RUN_TEST(test_second_attempt_succeeds_after_failure);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
