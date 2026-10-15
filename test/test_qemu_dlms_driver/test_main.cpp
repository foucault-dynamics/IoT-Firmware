/**
 * @file
 * Driver tests for DlmsCosemReader over a scripted FakeBus, run in QEMU.
 *
 * Covers reading HDLC frames off the bus: skipping noise and idle flags,
 * trusting the length field over flags inside the frame, replies split across
 * reads, and the 1000 ms timeout. Then opening and closing the link: the exact
 * SNRM and DISC sent, which replies are accepted, and the sequence reset.
 * Then I-frame exchanges: LLC headers, N(S) and N(R) in both directions, and
 * every way a reply is rejected. Then whole sessions through the getters,
 * including DISC after a failed read. millis() runs in real time under QEMU,
 * so the timeouts are real.
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

  /**
   * Calls DlmsCosemReader::connect().
   *
   * @param[in] r  Reader to call it on.
   * @return What connect() returned.
   */
  static int connect(DlmsCosemReader &r) { return r.connect(); }

  /**
   * Calls DlmsCosemReader::disconnect().
   *
   * @param[in] r  Reader to call it on.
   * @return What disconnect() returned.
   */
  static int disconnect(DlmsCosemReader &r) { return r.disconnect(); }

  /**
   * Sets DlmsCosemReader::vs and DlmsCosemReader::vr.
   *
   * @param[in] r   Reader to change.
   * @param[in] vs  New V(S).
   * @param[in] vr  New V(R).
   */
  static void setSequence(DlmsCosemReader &r, uint8_t vs, uint8_t vr) {
    r.vs = vs;
    r.vr = vr;
  }

  /**
   * Reads DlmsCosemReader::vs.
   *
   * @param[in] r  Reader to read it from.
   * @return V(S).
   */
  static uint8_t vs(const DlmsCosemReader &r) { return r.vs; }

  /**
   * Reads DlmsCosemReader::vr.
   *
   * @param[in] r  Reader to read it from.
   * @return V(R).
   */
  static uint8_t vr(const DlmsCosemReader &r) { return r.vr; }

  /**
   * Calls DlmsCosemReader::exchange().
   *
   * @param[in]  r        Reader to call it on.
   * @param[in]  apdu     APDU to send.
   * @param[out] resp     At least DLMS_APDU_MAX bytes.
   * @param[out] respLen  Length of @p resp.
   * @return What exchange() returned.
   */
  static int exchange(DlmsCosemReader &r, const std::vector<uint8_t> &apdu, uint8_t *resp,
                      size_t *respLen) {
    return r.exchange(apdu.data(), apdu.size(), resp, respLen);
  }
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
  const uint8_t importObis[6] = {1, 0, 1, 8, 0, 255};
  const uint8_t exportObis[6] = {1, 0, 2, 8, 0, 255};
  const uint8_t voltageObis[6] = {1, 0, 32, 7, 0, 255};
  memcpy(config.importObis, importObis, 6);
  memcpy(config.exportObis, exportObis, 6);
  memcpy(config.voltageObis, voltageObis, 6);
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

/** SNRM from client 16 to server 1, from the trace. */
const std::vector<uint8_t> SNRM = {0x7E, 0xA0, 0x07, 0x03, 0x21, 0x93, 0x0F, 0x01, 0x7E};

/** DISC from client 16 to server 1, from the trace. */
const std::vector<uint8_t> DISC = {0x7E, 0xA0, 0x07, 0x03, 0x21, 0x53, 0x03, 0xC7, 0x7E};

/**
 * Builds a reply frame with no info field.
 *
 * @param[in] control  Control byte.
 * @param[in] logical  Server logical address the reply comes from.
 * @param[in] sap      Client SAP the reply is addressed to.
 * @return The frame, FCS included.
 */
std::vector<uint8_t> reply(uint8_t control, uint16_t logical = 1, uint8_t sap = 16) {
  HdlcAddress dest;
  HdlcAddress src;
  hdlcAddress(sap, 0, 1, &dest);
  hdlcAddress(logical, 0, 1, &src);
  uint8_t buf[HDLC_FRAME_MAX];
  size_t len = 0;
  hdlcBuildFrame(&dest, &src, control, nullptr, 0, buf, &len);
  return std::vector<uint8_t>(buf, buf + len);
}

/**
 * Checks that the reader sent exactly one expected frame.
 *
 * @param[in] bus       Bus the reader sent on.
 * @param[in] expected  The frame.
 */
void assertSentOnly(const FakeBus &bus, const std::vector<uint8_t> &expected) {
  TEST_ASSERT_EQUAL_UINT(1, bus.sent.size());
  TEST_ASSERT_EQUAL_UINT(expected.size(), bus.sent[0].size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), bus.sent[0].data(), expected.size());
}

/**
 * Runs connect() against one scripted reply.
 *
 * @param[in] bus     Bus with the reply queued.
 * @param[in] reader  Reader to connect, init() is called on @p bus.
 * @return What connect() returned.
 */
int connectWith(FakeBus &bus, DlmsCosemReader &reader) {
  reader.init(bus);
  return DlmsCosemReaderTest::connect(reader);
}

/** A GET request APDU, its contents do not matter to exchange(). */
const std::vector<uint8_t> GET_APDU = {0xC0, 0x01, 0xC1, 0x00, 0x03, 0x01, 0x00,
                                       0x01, 0x08, 0x00, 0xFF, 0x02, 0x00};

/** A GET response APDU, its contents do not matter to exchange(). */
const std::vector<uint8_t> RESP_APDU = {0xC4, 0x01, 0xC1, 0x00, 0x06, 0x00, 0xBC, 0x61, 0x4E};

/**
 * Builds a reply I-frame from server 1 to client 16 by hand, with no limit on
 * the info length, so a test can send what hdlcBuildFrame() would refuse.
 *
 * @param[in] control  Control byte.
 * @param[in] info     Info field, LLC included.
 * @return The frame, HCS and FCS included.
 */
std::vector<uint8_t> iReply(uint8_t control, const std::vector<uint8_t> &info) {
  size_t length = 9 + info.size();
  std::vector<uint8_t> frame = {0x7E, static_cast<uint8_t>(0xA0 | (length >> 8)),
                                static_cast<uint8_t>(length), 0x21, 0x03, control};
  uint16_t hcs = hdlcFcs(frame.data() + 1, frame.size() - 1);
  frame.push_back(hcs & 0xFF);
  frame.push_back(hcs >> 8);
  frame.insert(frame.end(), info.begin(), info.end());
  uint16_t fcs = hdlcFcs(frame.data() + 1, frame.size() - 1);
  frame.push_back(fcs & 0xFF);
  frame.push_back(fcs >> 8);
  frame.push_back(0x7E);
  return frame;
}

/**
 * Builds a valid reply I-frame carrying RESP_APDU behind the response LLC.
 *
 * @param[in] control  Control byte.
 * @return The frame.
 */
std::vector<uint8_t> respFrame(uint8_t control) {
  return iReply(control, concat({0xE6, 0xE7, 0x00}, RESP_APDU));
}

/**
 * Runs one exchange of GET_APDU on a reader already set up.
 *
 * @param[in] reader  Reader to exchange on.
 * @return What exchange() returned.
 */
int exchangeGet(DlmsCosemReader &reader) {
  uint8_t resp[DLMS_APDU_MAX];
  size_t respLen = 0;
  return DlmsCosemReaderTest::exchange(reader, GET_APDU, resp, &respLen);
}

/**
 * Checks that one exchange fails against a reply and leaves the counters at 0.
 *
 * @param[in] reply  Frame the meter answers with.
 */
void assertExchangeRejects(const std::vector<uint8_t> &reply) {
  FakeBus bus;
  bus.queueReply(reply);
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, exchangeGet(reader));
  TEST_ASSERT_EQUAL_UINT8(0, DlmsCosemReaderTest::vs(reader));
  TEST_ASSERT_EQUAL_UINT8(0, DlmsCosemReaderTest::vr(reader));
}

/** AARQ from the trace, as buildAarq() writes it. */
const std::vector<uint8_t> AARQ = {
    0x60, 0x1D, 0xA1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08,
    0x01, 0x01, 0xBE, 0x10, 0x04, 0x0E, 0x01, 0x00, 0x00, 0x00, 0x06,
    0x5F, 0x1F, 0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x7D};

/** Accepted AARE from the trace, without LLC. */
const std::vector<uint8_t> AARE = {
    0x61, 0x29, 0xA1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08,
    0x01, 0x01, 0xA2, 0x03, 0x02, 0x01, 0x00, 0xA3, 0x05, 0xA1, 0x03,
    0x02, 0x01, 0x00, 0xBE, 0x10, 0x04, 0x0E, 0x08, 0x00, 0x06, 0x5F,
    0x1F, 0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x07};

/**
 * Builds a GET request APDU for one Register attribute.
 *
 * @param[in] c          OBIS value group C.
 * @param[in] d          OBIS value group D.
 * @param[in] attribute  Attribute id.
 * @return The APDU.
 */
std::vector<uint8_t> getApdu(uint8_t c, uint8_t d, uint8_t attribute) {
  return {0xC0, 0x01, 0xC1, 0x00, 0x03, 0x01, 0x00, c, d, 0x00, 0xFF, attribute, 0x00};
}

/**
 * Builds the I-frame the reader sends, from client 16 to server 1.
 *
 * @param[in] control  Control byte.
 * @param[in] apdu     APDU, without LLC.
 * @return The frame.
 */
std::vector<uint8_t> request(uint8_t control, const std::vector<uint8_t> &apdu) {
  HdlcAddress client;
  HdlcAddress server;
  hdlcAddress(16, 0, 1, &client);
  hdlcAddress(1, 0, 1, &server);
  std::vector<uint8_t> info = concat({0xE6, 0xE6, 0x00}, apdu);
  uint8_t buf[HDLC_FRAME_MAX];
  size_t len = 0;
  hdlcBuildFrame(&server, &client, control, info.data(), info.size(), buf, &len);
  return std::vector<uint8_t>(buf, buf + len);
}

/**
 * Builds a reply I-frame from server 1 carrying an APDU behind the response LLC.
 *
 * @param[in] control  Control byte.
 * @param[in] apdu     APDU, without LLC.
 * @return The frame.
 */
std::vector<uint8_t> respond(uint8_t control, const std::vector<uint8_t> &apdu) {
  return iReply(control, concat({0xE6, 0xE7, 0x00}, apdu));
}

/**
 * Builds a get-response-normal carrying Data.
 *
 * @param[in] data  Data, starting at its type tag.
 * @return The APDU.
 */
std::vector<uint8_t> getResponse(const std::vector<uint8_t> &data) {
  return concat({0xC4, 0x01, 0xC1, 0x00}, data);
}

/**
 * Queues a whole session's replies: UA, the AARE, scaler_unit, value, and UA
 * for the DISC.
 *
 * @param[in] bus         Bus to queue them on.
 * @param[in] scalerUnit  Data of the scaler_unit response.
 * @param[in] value       Data of the value response.
 */
void queueSession(FakeBus &bus, const std::vector<uint8_t> &scalerUnit,
                  const std::vector<uint8_t> &value) {
  bus.queueReply(UA);
  bus.queueReply(respond(0x30, AARE));
  bus.queueReply(respond(0x52, getResponse(scalerUnit)));
  bus.queueReply(respond(0x74, getResponse(value)));
  bus.queueReply(UA);
}

/**
 * Checks every frame the reader sent, in order.
 *
 * @param[in] bus       Bus the reader sent on.
 * @param[in] expected  Frames it should have sent.
 */
void assertSent(const FakeBus &bus, const std::vector<std::vector<uint8_t>> &expected) {
  TEST_ASSERT_EQUAL_UINT(expected.size(), bus.sent.size());
  for (size_t i = 0; i < expected.size(); i++) {
    TEST_ASSERT_EQUAL_UINT(expected[i].size(), bus.sent[i].size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected[i].data(), bus.sent[i].data(), expected[i].size());
  }
}

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/**
 * @defgroup test_qemu_dlms_driver DLMS/COSEM driver
 * @ingroup tests
 * Tests in test_qemu_dlms_driver/test_main.cpp.
 * @{
 */

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

/** connect() sends exactly the SNRM from the trace. */
void test_connect_sends_snrm() {
  FakeBus bus;
  bus.queueReply(reply(HDLC_UA));
  DlmsCosemReader reader(makeConfig());
  connectWith(bus, reader);
  assertSentOnly(bus, SNRM);
}

/** A UA opens the link. */
void test_connect_accepts_ua() {
  FakeBus bus;
  bus.queueReply(UA);
  DlmsCosemReader reader(makeConfig());
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, connectWith(bus, reader));
}

/** A UA resets both sequence counters. */
void test_connect_resets_sequence() {
  FakeBus bus;
  bus.queueReply(UA);
  DlmsCosemReader reader(makeConfig());
  DlmsCosemReaderTest::setSequence(reader, 3, 5);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, connectWith(bus, reader));
  TEST_ASSERT_EQUAL_UINT8(0, DlmsCosemReaderTest::vs(reader));
  TEST_ASSERT_EQUAL_UINT8(0, DlmsCosemReaderTest::vr(reader));
}

/** A DM refuses the link and leaves the counters alone. */
void test_connect_rejects_dm() {
  FakeBus bus;
  bus.queueReply(reply(HDLC_DM));
  DlmsCosemReader reader(makeConfig());
  DlmsCosemReaderTest::setSequence(reader, 3, 5);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, connectWith(bus, reader));
  TEST_ASSERT_EQUAL_UINT8(3, DlmsCosemReaderTest::vs(reader));
  TEST_ASSERT_EQUAL_UINT8(5, DlmsCosemReaderTest::vr(reader));
}

/** A UA from another server is not ours. */
void test_connect_rejects_wrong_server() {
  FakeBus bus;
  bus.queueReply(reply(HDLC_UA, 2));
  DlmsCosemReader reader(makeConfig());
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, connectWith(bus, reader));
}

/** A UA addressed to another client is not ours. */
void test_connect_rejects_wrong_client() {
  FakeBus bus;
  bus.queueReply(reply(HDLC_UA, 1, 17));
  DlmsCosemReader reader(makeConfig());
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, connectWith(bus, reader));
}

/** A UA with a bad FCS is rejected. */
void test_connect_rejects_corrupt_ua() {
  std::vector<uint8_t> corrupt = UA;
  corrupt[7] ^= 0xFF;
  FakeBus bus;
  bus.queueReply(corrupt);
  DlmsCosemReader reader(makeConfig());
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, connectWith(bus, reader));
}

/** A meter that never answers the SNRM fails after the timeout. */
void test_connect_rejects_silence() {
  FakeBus bus;
  bus.queueSilence();
  DlmsCosemReader reader(makeConfig());
  uint32_t start = millis();
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, connectWith(bus, reader));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(DLMS_TIMEOUT, millis() - start);
}

/** disconnect() sends exactly the DISC from the trace, and a UA closes the link. */
void test_disconnect_sends_disc() {
  FakeBus bus;
  bus.queueReply(UA);
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DlmsCosemReaderTest::disconnect(reader));
  assertSentOnly(bus, DISC);
}

/** A DM to DISC means already disconnected, which is still success. */
void test_disconnect_accepts_dm() {
  FakeBus bus;
  bus.queueReply(reply(HDLC_DM));
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DlmsCosemReaderTest::disconnect(reader));
}

/** The info field sent is the command LLC followed by the APDU. */
void test_exchange_sends_llc_and_apdu() {
  FakeBus bus;
  bus.queueReply(respFrame(0x30));
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, exchangeGet(reader));
  std::vector<uint8_t> info = concat({0xE6, 0xE6, 0x00}, GET_APDU);
  const std::vector<uint8_t> &sent = bus.sent[0];
  TEST_ASSERT_EQUAL_UINT(info.size() + 11, sent.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(info.data(), sent.data() + 8, info.size());
}

/** Two exchanges send controls 10 then 32, as in the trace. */
void test_exchange_controls_advance() {
  FakeBus bus;
  bus.queueReply(respFrame(0x30));
  bus.queueReply(respFrame(0x52));
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, exchangeGet(reader));
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, exchangeGet(reader));
  TEST_ASSERT_EQUAL_HEX8(0x10, bus.sent[0][5]);
  TEST_ASSERT_EQUAL_HEX8(0x32, bus.sent[1][5]);
  TEST_ASSERT_EQUAL_UINT8(2, DlmsCosemReaderTest::vs(reader));
  TEST_ASSERT_EQUAL_UINT8(2, DlmsCosemReaderTest::vr(reader));
}

/** The reply's APDU comes back without its LLC header. */
void test_exchange_returns_apdu() {
  FakeBus bus;
  bus.queueReply(respFrame(0x30));
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  uint8_t resp[DLMS_APDU_MAX];
  size_t respLen = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DlmsCosemReaderTest::exchange(reader, GET_APDU, resp, &respLen));
  TEST_ASSERT_EQUAL_UINT(RESP_APDU.size(), respLen);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(RESP_APDU.data(), resp, respLen);
}

/** From 7 and 7 the control is FE, and both counters wrap to 0. */
void test_exchange_wraps_mod_8() {
  FakeBus bus;
  bus.queueReply(respFrame(0x1E));
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  DlmsCosemReaderTest::setSequence(reader, 7, 7);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, exchangeGet(reader));
  TEST_ASSERT_EQUAL_HEX8(0xFE, bus.sent[0][5]);
  TEST_ASSERT_EQUAL_UINT8(0, DlmsCosemReaderTest::vs(reader));
  TEST_ASSERT_EQUAL_UINT8(0, DlmsCosemReaderTest::vr(reader));
}

/** A reply numbered 1 when 0 was expected is rejected. */
void test_exchange_rejects_wrong_ns() {
  assertExchangeRejects(respFrame(0x32));
}

/** A reply that does not acknowledge the frame sent is rejected. */
void test_exchange_rejects_wrong_nr() {
  assertExchangeRejects(respFrame(0x10));
}

/** A reply with the command LLC instead of the response LLC is rejected. */
void test_exchange_rejects_wrong_llc() {
  assertExchangeRejects(iReply(0x30, concat({0xE6, 0xE6, 0x00}, RESP_APDU)));
}

/** A DM in the middle of a session is rejected. */
void test_exchange_rejects_u_frame() {
  assertExchangeRejects(reply(HDLC_DM));
}

/** A reply with the final bit clear is segmented, and rejected. */
void test_exchange_rejects_segmented() {
  assertExchangeRejects(respFrame(0x20));
}

/** A reply with more info than HDLC_INFO_MAX is rejected, not copied. */
void test_exchange_rejects_oversized_reply() {
  std::vector<uint8_t> info(HDLC_INFO_MAX + 2, 0x00);
  info[0] = 0xE6;
  info[1] = 0xE7;
  assertExchangeRejects(iReply(0x30, info));
}

/** An APDU too long for one frame is rejected before anything is sent. */
void test_exchange_rejects_oversized_apdu() {
  FakeBus bus;
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  std::vector<uint8_t> apdu(DLMS_APDU_MAX + 1, 0x00);
  uint8_t resp[DLMS_APDU_MAX];
  size_t respLen = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, DlmsCosemReaderTest::exchange(reader, apdu, resp, &respLen));
  TEST_ASSERT_EQUAL_UINT(0, bus.sent.size());
}

/** The trace's whole session reads 12345.678 kWh, and every frame sent matches it. */
void test_session_import() {
  FakeBus bus;
  queueSession(bus, {0x02, 0x02, 0x0F, 0x00, 0x16, 0x1E}, {0x06, 0x00, 0xBC, 0x61, 0x4E});
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  float val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_import(&val));
  TEST_ASSERT_EQUAL_FLOAT(12345.678f, val);
  assertSent(bus, {SNRM, request(0x10, AARQ), request(0x32, getApdu(1, 8, DLMS_ATTR_SCALER_UNIT)),
                   request(0x54, getApdu(1, 8, DLMS_ATTR_VALUE)), DISC});
}

/** Export reads OBIS 1.0.2.8.0.255, here with scaler 3 so 12 is 12 kWh. */
void test_session_export() {
  FakeBus bus;
  queueSession(bus, {0x02, 0x02, 0x0F, 0x03, 0x16, 0x1E}, {0x12, 0x00, 0x0C});
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  float val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_export(&val));
  TEST_ASSERT_EQUAL_FLOAT(12.0f, val);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(request(0x54, getApdu(2, 8, DLMS_ATTR_VALUE)).data(),
                               bus.sent[3].data(), bus.sent[3].size());
}

/** Voltage reads OBIS 1.0.32.7.0.255 in tenths of a volt, 2301 is 230.1 V. */
void test_session_voltage() {
  FakeBus bus;
  queueSession(bus, {0x02, 0x02, 0x0F, 0xFF, 0x16, 0x23}, {0x12, 0x08, 0xFD});
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  float val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_voltage(&val));
  TEST_ASSERT_EQUAL_FLOAT(230.1f, val);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(request(0x54, getApdu(32, 7, DLMS_ATTR_VALUE)).data(),
                               bus.sent[3].data(), bus.sent[3].size());
}

/** A rejected AARE fails the read, and DISC is still sent. */
void test_session_rejected_aare_sends_disc() {
  std::vector<uint8_t> rejected = AARE;
  rejected[17] = 0x01;
  rejected[24] = 0x01;
  FakeBus bus;
  bus.queueReply(UA);
  bus.queueReply(respond(0x30, rejected));
  bus.queueReply(UA);
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  float val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_import(&val));
  assertSent(bus, {SNRM, request(0x10, AARQ), DISC});
}

/** An object the meter does not have fails the read, and DISC is still sent. */
void test_session_object_undefined_sends_disc() {
  FakeBus bus;
  bus.queueReply(UA);
  bus.queueReply(respond(0x30, AARE));
  bus.queueReply(respond(0x52, {0xC4, 0x01, 0xC1, 0x01, 0x04}));
  bus.queueReply(UA);
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  float val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_import(&val));
  assertSent(bus, {SNRM, request(0x10, AARQ), request(0x32, getApdu(1, 8, DLMS_ATTR_SCALER_UNIT)),
                   DISC});
}

/** A register in V read as energy fails before its value is asked for. */
void test_session_wrong_unit_fails() {
  FakeBus bus;
  queueSession(bus, {0x02, 0x02, 0x0F, 0x00, 0x16, 0x23}, {0x06, 0x00, 0xBC, 0x61, 0x4E});
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  float val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_import(&val));
  TEST_ASSERT_EQUAL_UINT(4, bus.sent.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(DISC.data(), bus.sent[3].data(), DISC.size());
}

/** A refused SNRM fails without a DISC, since the link never came up. */
void test_session_refused_link_skips_disc() {
  FakeBus bus;
  bus.queueReply(reply(HDLC_DM));
  DlmsCosemReader reader(makeConfig());
  reader.init(bus);
  float val = 0;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_import(&val));
  assertSent(bus, {SNRM});
}

/** @} */

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
  RUN_TEST(test_connect_sends_snrm);
  RUN_TEST(test_connect_accepts_ua);
  RUN_TEST(test_connect_resets_sequence);
  RUN_TEST(test_connect_rejects_dm);
  RUN_TEST(test_connect_rejects_wrong_server);
  RUN_TEST(test_connect_rejects_wrong_client);
  RUN_TEST(test_connect_rejects_corrupt_ua);
  RUN_TEST(test_connect_rejects_silence);
  RUN_TEST(test_disconnect_sends_disc);
  RUN_TEST(test_disconnect_accepts_dm);
  RUN_TEST(test_exchange_sends_llc_and_apdu);
  RUN_TEST(test_exchange_controls_advance);
  RUN_TEST(test_exchange_returns_apdu);
  RUN_TEST(test_exchange_wraps_mod_8);
  RUN_TEST(test_exchange_rejects_wrong_ns);
  RUN_TEST(test_exchange_rejects_wrong_nr);
  RUN_TEST(test_exchange_rejects_wrong_llc);
  RUN_TEST(test_exchange_rejects_u_frame);
  RUN_TEST(test_exchange_rejects_segmented);
  RUN_TEST(test_exchange_rejects_oversized_reply);
  RUN_TEST(test_exchange_rejects_oversized_apdu);
  RUN_TEST(test_session_import);
  RUN_TEST(test_session_export);
  RUN_TEST(test_session_voltage);
  RUN_TEST(test_session_rejected_aare_sends_disc);
  RUN_TEST(test_session_object_undefined_sends_disc);
  RUN_TEST(test_session_wrong_unit_fails);
  RUN_TEST(test_session_refused_link_skips_disc);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
