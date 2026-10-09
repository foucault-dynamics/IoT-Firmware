/**
 * @file
 * Driver tests for DlmsCosemReader over a scripted FakeBus, run in QEMU.
 *
 * Covers reading HDLC frames off the bus: skipping noise and idle flags,
 * trusting the length field over flags inside the frame, replies split across
 * reads, and the 1000 ms timeout. millis() runs in real time under QEMU, so the
 * timeouts are real.
 */

#include <Arduino.h>
#include <unity.h>

#include <vector>

#include "../support/fake_bus.h"
#include "dlms_cosem.h"
#include "hdlc.h"

/** Forwards to DlmsCosemReader's private helpers. */
class DlmsCosemReaderTest {
 public:
  /**
   * Calls DlmsCosemReader::readFrame().
   *
   * @param[in]  r    Reader to call it on.
   * @param[out] buf  At least HDLC_FRAME_MAX bytes.
   * @param[out] len  Frame length.
   * @return What readFrame() returned.
   */
  static int readFrame(DlmsCosemReader &r, uint8_t *buf, size_t *len) { return r.readFrame(buf, len); }
};

namespace {

/** UA from server 1 to client 16, from the trace. */
const std::vector<uint8_t> UA = {0x7E, 0xA0, 0x07, 0x21, 0x03, 0x73, 0x01, 0x40, 0x7E};

/**
 * Builds the config every test reads with: client 16, server 1, one byte.
 *
 * @return The config.
 */
DlmsCosemConfig makeConfig() {
  DlmsCosemConfig config = {};
  config.clientSap = 16;
  config.serverLogical = 1;
  config.serverAddrLen = 1;
  return config;
}

/**
 * Joins two byte lists.
 *
 * @param[in] a  First part.
 * @param[in] b  Second part.
 * @return @p a followed by @p b.
 */
std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

/**
 * Starts the bus's next scripted reply and reads one frame from it.
 *
 * FakeBus only replays a reply after a send(), so a dummy byte is sent first.
 *
 * @param[in]  bus    Bus with the reply queued.
 * @param[out] frame  The frame read, empty on failure.
 * @return What readFrame() returned.
 */
int readFrom(FakeBus &bus, std::vector<uint8_t> &frame) {
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  const uint8_t dummy = 0;
  bus.send(&dummy, 1);
  uint8_t buf[HDLC_FRAME_MAX];
  size_t len = 0;
  int result = DlmsCosemReaderTest::readFrame(reader, buf, &len);
  frame.assign(buf, buf + (result == EXIT_SUCCESS ? len : 0));
  return result;
}

/**
 * Checks that the bus yields exactly one expected frame.
 *
 * @param[in] bus       Bus with the reply queued.
 * @param[in] expected  Frame readFrame() should return.
 */
void assertReads(FakeBus &bus, const std::vector<uint8_t> &expected) {
  std::vector<uint8_t> frame;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, readFrom(bus, frame));
  TEST_ASSERT_EQUAL_UINT(expected.size(), frame.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), frame.data(), expected.size());
}

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/** A frame arriving on its own is read whole. */
void test_read_whole_frame() {
  FakeBus bus;
  bus.queueReply(UA);
  assertReads(bus, UA);
}

/** Noise and idle flags before the frame are skipped. */
void test_read_skips_noise_and_idle_flags() {
  FakeBus bus;
  bus.queueReply(concat({0x00, 0xFF, 0x7E, 0x7E, 0x7E}, UA));
  assertReads(bus, UA);
}

/** A flag followed by a byte that is not type A was noise, the hunt restarts. */
void test_read_resyncs_after_false_flag() {
  FakeBus bus;
  bus.queueReply(concat({0x7E, 0x12}, UA));
  assertReads(bus, UA);
}

/** A flag byte inside the info field does not end the frame. */
void test_read_frame_with_flag_in_info() {
  HdlcAddress client;
  HdlcAddress server;
  hdlcAddress(16, 0, 1, &client);
  hdlcAddress(1, 0, 1, &server);
  const uint8_t info[] = {0xE6, 0xE7, 0x00, 0x7E, 0x7E, 0x01};
  uint8_t buf[HDLC_FRAME_MAX];
  size_t len = 0;
  hdlcBuildFrame(&client, &server, 0x30, info, sizeof(info), buf, &len);
  std::vector<uint8_t> frame(buf, buf + len);

  FakeBus bus;
  bus.queueReply(frame);
  assertReads(bus, frame);
}

/** A frame split into two chunks 50 ms apart is joined. */
void test_read_split_across_chunks() {
  FakeBus bus;
  bus.queueChunks({{{UA.begin(), UA.begin() + 4}, 0}, {{UA.begin() + 4, UA.end()}, 50000}});
  assertReads(bus, UA);
}

/** A meter that never replies fails the read after the 1000 ms timeout. */
void test_read_silence_times_out() {
  FakeBus bus;
  bus.queueSilence();
  std::vector<uint8_t> frame;
  uint32_t start = millis();
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, readFrom(bus, frame));
  uint32_t elapsed = millis() - start;
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(DLMS_TIMEOUT, elapsed);
  TEST_ASSERT_LESS_THAN_UINT32(DLMS_TIMEOUT + 200, elapsed);
}

/** A frame cut short times out rather than being returned. */
void test_read_partial_frame_times_out() {
  FakeBus bus;
  bus.queueReply({UA.begin(), UA.begin() + 5});
  std::vector<uint8_t> frame;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, readFrom(bus, frame));
}

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_read_whole_frame);
  RUN_TEST(test_read_skips_noise_and_idle_flags);
  RUN_TEST(test_read_resyncs_after_false_flag);
  RUN_TEST(test_read_frame_with_flag_in_info);
  RUN_TEST(test_read_split_across_chunks);
  RUN_TEST(test_read_silence_times_out);
  RUN_TEST(test_read_partial_frame_times_out);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
