/**
 * @file
 * Driver tests for CamHttpReader over a scripted FakeBus, run in QEMU.
 *
 * FakeBus stands in for HttpBus: send() records the URL the reader asks for
 * and readByte() hands back a scripted response body, shaped like the "/json"
 * API of AI-on-the-edge-device.
 */

#include <Arduino.h>
#include <unity.h>

#include <cstring>
#include <string>

#include "../support/fake_bus.h"
#include "cam_http.h"

namespace {

/** Written to outputs first, to check a failed read leaves them alone. */
constexpr float SENTINEL = -999.0f;

/** A good read of flow "main", as the cam reports it. */
const char *GOOD_BODY =
    "{\"main\":{\"value\":\"12345.678\",\"raw\":\"12345.678\",\"pre\":\"12345.678\","
    "\"error\":\"no error\",\"rate\":\"0.000\",\"timestamp\":\"2026-10-10T10:00:00+1000\"}}";

/**
 * Builds a config for the cam at 192.168.4.2, flow "main".
 *
 * @return The config.
 */
CamHttpConfig camConfig() {
  CamHttpConfig cfg{};
  strncpy(cfg.host, "192.168.4.2", sizeof(cfg.host) - 1);
  strncpy(cfg.path, "/json", sizeof(cfg.path) - 1);
  strncpy(cfg.flowName, "main", sizeof(cfg.flowName) - 1);
  return cfg;
}

/**
 * Reads the import register through a fresh reader with one scripted body.
 *
 * @param[in]  body  Response body, or nullptr for an empty one.
 * @param[out] val   Reading, set to SENTINEL first.
 * @return What get_import() returned.
 */
int importWithBody(const char *body, float *val) {
  FakeBus bus;
  if (body == nullptr) {
    bus.queueSilence();
  } else {
    bus.queueText(body);
  }
  CamHttpReader reader(camConfig());
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.init(bus));
  *val = SENTINEL;
  return reader.get_import(val);
}

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/**
 * @defgroup test_qemu_cam_http Camera HTTP driver
 * @ingroup tests
 * Tests in test_qemu_cam_http/test_main.cpp.
 * @{
 */

/** The request is "http://" plus the host and the API path. */
void test_requests_json_url() {
  FakeBus bus;
  bus.queueText(GOOD_BODY);
  CamHttpReader reader(camConfig());
  reader.init(bus);
  float val;
  reader.get_import(&val);
  TEST_ASSERT_EQUAL_size_t(1, bus.sent.size());
  std::string url(bus.sent[0].begin(), bus.sent[0].end());
  TEST_ASSERT_EQUAL_STRING("http://192.168.4.2/json", url.c_str());
}

/** A good read of the flow gives its value as the import reading. */
void test_good_read_gives_value() {
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, importWithBody(GOOD_BODY, &val));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12345.678f, val);
}

/** An empty error field also counts as a good read. */
void test_empty_error_is_good() {
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, importWithBody("{\"main\":{\"value\":\"42.5\",\"error\":\"\"}}", &val));
  TEST_ASSERT_EQUAL_FLOAT(42.5f, val);
}

/** An error reported by the cam fails the read and leaves the output alone. */
void test_cam_error_fails() {
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE,
                        importWithBody("{\"main\":{\"value\":\"42.5\",\"error\":\"Neg. Rate - Read: 41.0\"}}", &val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
}

/** A response without the configured flow fails. */
void test_missing_flow_fails() {
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, importWithBody("{\"other\":{\"value\":\"42.5\",\"error\":\"no error\"}}", &val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
}

/** A flow with no value yet, as just after the cam boots, fails. */
void test_empty_value_fails() {
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, importWithBody("{\"main\":{\"value\":\"\",\"error\":\"no error\"}}", &val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
}

/** A body that is not valid JSON fails. */
void test_malformed_json_fails() {
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, importWithBody("{\"main\":{\"value\":\"42.5\"", &val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
}

/** An empty response body fails. */
void test_empty_body_fails() {
  float val;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, importWithBody(nullptr, &val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
}

/** init() rejects a config with no host. */
void test_init_rejects_empty_host() {
  FakeBus bus;
  CamHttpConfig cfg = camConfig();
  cfg.host[0] = '\0';
  CamHttpReader reader(cfg);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.init(bus));
}

/** init() rejects a config with no flow name. */
void test_init_rejects_empty_flow() {
  FakeBus bus;
  CamHttpConfig cfg = camConfig();
  cfg.flowName[0] = '\0';
  CamHttpReader reader(cfg);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.init(bus));
}

/** With no export flow configured, export fails without sending anything, and voltage always does. */
void test_export_and_voltage_unsupported() {
  FakeBus bus;
  CamHttpReader reader(camConfig());
  reader.init(bus);
  float val = SENTINEL;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_export(&val));
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_voltage(&val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
  TEST_ASSERT_EQUAL_size_t(0, bus.sent.size());
}

/** Two reads in a row each send a request and each get their own value. */
void test_consecutive_reads() {
  FakeBus bus;
  bus.queueText("{\"main\":{\"value\":\"100.1\",\"error\":\"no error\"}}");
  bus.queueText("{\"main\":{\"value\":\"100.2\",\"error\":\"no error\"}}");
  CamHttpReader reader(camConfig());
  reader.init(bus);
  float first;
  float second;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_import(&first));
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_import(&second));
  TEST_ASSERT_EQUAL_FLOAT(100.1f, first);
  TEST_ASSERT_EQUAL_FLOAT(100.2f, second);
  TEST_ASSERT_EQUAL_size_t(2, bus.sent.size());
}

/** With an export flow configured, export reads that flow and import still reads its own. */
void test_export_flow_read() {
  const char *body =
      "{\"main\":{\"value\":\"100.5\",\"error\":\"no error\"},"
      "\"export\":{\"value\":\"20.25\",\"error\":\"no error\"}}";
  FakeBus bus;
  bus.queueText(body);
  bus.queueText(body);
  CamHttpConfig cfg = camConfig();
  strncpy(cfg.exportFlowName, "export", sizeof(cfg.exportFlowName) - 1);
  CamHttpReader reader(cfg);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.init(bus));
  float importVal = SENTINEL;
  float exportVal = SENTINEL;
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_import(&importVal));
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.get_export(&exportVal));
  TEST_ASSERT_EQUAL_FLOAT(100.5f, importVal);
  TEST_ASSERT_EQUAL_FLOAT(20.25f, exportVal);
}

/** An export flow the cam does not report fails and leaves the output alone. */
void test_export_flow_missing_fails() {
  FakeBus bus;
  bus.queueText(GOOD_BODY);
  CamHttpConfig cfg = camConfig();
  strncpy(cfg.exportFlowName, "export", sizeof(cfg.exportFlowName) - 1);
  CamHttpReader reader(cfg);
  reader.init(bus);
  float val = SENTINEL;
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.get_export(&val));
  TEST_ASSERT_EQUAL_FLOAT(SENTINEL, val);
}

/** probe() passes on a cam that names the flow but has not read the meter yet. */
void test_probe_passes_before_first_reading() {
  FakeBus bus;
  bus.queueText("{\"main\":{\"value\":\"\",\"error\":\"no error\"}}");
  CamHttpReader reader(camConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, reader.probe());
  TEST_ASSERT_EQUAL_size_t(1, bus.sent.size());
}

/** probe() fails when the cam does not answer. */
void test_probe_fails_without_answer() {
  FakeBus bus;
  bus.queueSilence();
  CamHttpReader reader(camConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.probe());
}

/** probe() fails when the cam does not report the import flow. */
void test_probe_fails_on_missing_flow() {
  FakeBus bus;
  bus.queueText("{\"other\":{\"value\":\"42.5\",\"error\":\"no error\"}}");
  CamHttpReader reader(camConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.probe());
}

/** probe() fails when an export flow is configured but the cam does not report it. */
void test_probe_fails_on_missing_export_flow() {
  FakeBus bus;
  bus.queueText(GOOD_BODY);
  CamHttpConfig cfg = camConfig();
  strncpy(cfg.exportFlowName, "export", sizeof(cfg.exportFlowName) - 1);
  CamHttpReader reader(cfg);
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.probe());
}

/** probe() fails when the cam answers with something that is not JSON. */
void test_probe_fails_on_malformed_json() {
  FakeBus bus;
  bus.queueText("{\"main\":");
  CamHttpReader reader(camConfig());
  reader.init(bus);
  TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, reader.probe());
}

/** @} */

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_requests_json_url);
  RUN_TEST(test_good_read_gives_value);
  RUN_TEST(test_empty_error_is_good);
  RUN_TEST(test_cam_error_fails);
  RUN_TEST(test_missing_flow_fails);
  RUN_TEST(test_empty_value_fails);
  RUN_TEST(test_malformed_json_fails);
  RUN_TEST(test_empty_body_fails);
  RUN_TEST(test_init_rejects_empty_host);
  RUN_TEST(test_init_rejects_empty_flow);
  RUN_TEST(test_export_and_voltage_unsupported);
  RUN_TEST(test_consecutive_reads);
  RUN_TEST(test_export_flow_read);
  RUN_TEST(test_export_flow_missing_fails);
  RUN_TEST(test_probe_passes_before_first_reading);
  RUN_TEST(test_probe_fails_without_answer);
  RUN_TEST(test_probe_fails_on_missing_flow);
  RUN_TEST(test_probe_fails_on_missing_export_flow);
  RUN_TEST(test_probe_fails_on_malformed_json);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
