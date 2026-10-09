/**
 * @file
 * Unit tests for Modbus RTU frame encoding, run in QEMU.
 *
 * Reaches ModbusRtuReader's private CRC and request builder through the
 * ModbusRtuReaderTest friend class.
 */

#include <Arduino.h>
#include <unity.h>

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

  /**
   * Calls ModbusRtuReader::build_request().
   *
   * @param[in]  r     Reader to call it on.
   * @param[out] out   At least REQUEST_LEN bytes.
   * @param[in]  addr  Start address of the register pair.
   */
  static void request(ModbusRtuReader &r, uint8_t *out, uint16_t addr) { r.build_request(out, addr); }
};

namespace {

/**
 * Builds a reader for slave 1 using function code 0x03 over 8N1.
 *
 * @return The reader. init() is not called, nothing touches a bus.
 */
ModbusRtuReader makeReader() {
  ModbusRtuConfig config = {};
  config.slaveAddress = 1;
  config.functionCode = 0x03;
  config.bus.format = SERIAL_8N1;
  return ModbusRtuReader(config);
}

/**
 * Checks the CRC of a 6 byte request against its expected wire bytes.
 *
 * @param[in] frame  The 6 bytes before the CRC.
 * @param[in] lo     Expected first CRC byte on the wire.
 * @param[in] hi     Expected second CRC byte on the wire.
 */
void assertCrc(const uint8_t *frame, uint8_t lo, uint8_t hi) {
  ModbusRtuReader reader = makeReader();
  uint16_t crc = ModbusRtuReaderTest::crc(reader, frame, 6);
  TEST_ASSERT_EQUAL_HEX8(lo, crc & 0xFF);
  TEST_ASSERT_EQUAL_HEX8(hi, crc >> 8);
}

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/** CRC of 01 03 00 00 00 01 is 84 0A. */
void test_crc_read_one_register_at_0() {
  const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
  assertCrc(frame, 0x84, 0x0A);
}

/** CRC of 01 03 00 00 00 02 is C4 0B. */
void test_crc_read_two_registers_at_0() {
  const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
  assertCrc(frame, 0xC4, 0x0B);
}

/** CRC of 01 03 00 04 00 02 is 85 CA. */
void test_crc_read_two_registers_at_4() {
  const uint8_t frame[] = {0x01, 0x03, 0x00, 0x04, 0x00, 0x02};
  assertCrc(frame, 0x85, 0xCA);
}

/** CRC of 01 03 00 02 00 02 is 65 CB. */
void test_crc_read_two_registers_at_2() {
  const uint8_t frame[] = {0x01, 0x03, 0x00, 0x02, 0x00, 0x02};
  assertCrc(frame, 0x65, 0xCB);
}

/** A request for address 4 is 01 03 00 04 00 02 85 CA. */
void test_build_request_address_4() {
  ModbusRtuReader reader = makeReader();
  uint8_t out[REQUEST_LEN] = {};
  ModbusRtuReaderTest::request(reader, out, 0x0004);
  const uint8_t expected[REQUEST_LEN] = {0x01, 0x03, 0x00, 0x04, 0x00, 0x02, 0x85, 0xCA};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, REQUEST_LEN);
}

/** A request for address 0 is 01 03 00 00 00 02 C4 0B. */
void test_build_request_address_0() {
  ModbusRtuReader reader = makeReader();
  uint8_t out[REQUEST_LEN] = {};
  ModbusRtuReaderTest::request(reader, out, 0x0000);
  const uint8_t expected[REQUEST_LEN] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, REQUEST_LEN);
}

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_crc_read_one_register_at_0);
  RUN_TEST(test_crc_read_two_registers_at_0);
  RUN_TEST(test_crc_read_two_registers_at_4);
  RUN_TEST(test_crc_read_two_registers_at_2);
  RUN_TEST(test_build_request_address_4);
  RUN_TEST(test_build_request_address_0);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}