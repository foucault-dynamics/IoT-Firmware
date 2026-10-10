/**
 * @file
 * Tests for the NVS config readers, every node's loader and the sequence
 * counter, run in QEMU against its emulated flash.
 *
 * Every run boots a freshly merged flash image, so NVS starts empty. setUp()
 * also wipes both namespaces, so each test starts from defaults.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <unity.h>

#include "nvs_config.h"
#include "nvs_read.h"
#include "secrets.h"
#include "seq_counter.h"

namespace {

/** Namespace seq_counter.cpp keeps the counter in. */
const char *RUNTIME_NAMESPACE = "runtime";
/** Key seq_counter.cpp keeps the counter under. */
const char *SEQ_KEY = "seq";

/**
 * Wipes one NVS namespace.
 *
 * @param[in] ns  Namespace to wipe.
 */
void clearNamespace(const char *ns) {
  Preferences p;
  p.begin(ns, false);
  p.clear();
  p.end();
}

/**
 * Overwrites the stored sequence number behind seq_counter's back.
 *
 * @param[in] value  Value to store.
 */
void storeSeq(uint32_t value) {
  Preferences p;
  p.begin(RUNTIME_NAMESPACE, false);
  p.putUInt(SEQ_KEY, value);
  p.end();
}

/**
 * Reads the stored sequence number with a separate handle.
 *
 * @return The stored value, or 0 if it was never set.
 */
uint32_t storedSeq() {
  Preferences p;
  p.begin(RUNTIME_NAMESPACE, true);
  uint32_t value = p.getUInt(SEQ_KEY, 0);
  p.end();
  return value;
}

}  // namespace

/** Wipes NVS and opens the shared config handle for writing. */
void setUp() {
  clearNamespace(NVS_NAMESPACE);
  clearNamespace(RUNTIME_NAMESPACE);
  prefs.begin(NVS_NAMESPACE, false);
}

/** Closes the shared config handle. */
void tearDown() {
  prefs.end();
}

/**
 * @defgroup test_qemu_nvs NVS config
 * @ingroup tests
 * Tests in test_qemu_nvs/test_main.cpp.
 * @{
 */

/** A number written to NVS reads back. */
void test_read_u32_written_key() {
  prefs.putUInt("poll_ms", 5000);
  TEST_ASSERT_EQUAL_UINT32(5000, readU32("poll_ms", 1000));
}

/** An unset number gives the default. */
void test_read_u32_unset_key_gives_default() {
  TEST_ASSERT_EQUAL_UINT32(1000, readU32("poll_ms", 1000));
}

/** A string written to NVS reads back. */
void test_read_str_written_key() {
  prefs.putString("ap_ssid", "Kaizen");
  char out[16];
  readStr("ap_ssid", "Default", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Kaizen", out);
}

/** An unset string gives the default. */
void test_read_str_unset_key_gives_default() {
  char out[16];
  readStr("ap_ssid", "Default", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Default", out);
}

/** A string longer than the buffer is truncated and still terminated. */
void test_read_str_truncates() {
  prefs.putString("ap_ssid", "KaizenNetwork");
  char out[7];
  readStr("ap_ssid", "Default", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Kaizen", out);
}

/** A MAC written as text reads back as bytes. */
void test_read_mac_written_key() {
  prefs.putString("sub_mac", "aa:bb:cc:01:02:03");
  const uint8_t fallback[6] = {0};
  uint8_t out[6];
  readMac("sub_mac", fallback, out);
  const uint8_t expected[6] = {0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 6);
}

/** An unset or malformed MAC gives the default. */
void test_read_mac_falls_back() {
  const uint8_t fallback[6] = {1, 2, 3, 4, 5, 6};
  uint8_t out[6] = {0};
  readMac("sub_mac", fallback, out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fallback, out, 6);

  prefs.putString("sub_mac", "not a mac");
  memset(out, 0, sizeof(out));
  readMac("sub_mac", fallback, out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fallback, out, 6);
}

/** The substation loader uses a value set in NVS, and the default otherwise. */
void test_substation_loader_reads_nvs() {
  prefs.end();
  TEST_ASSERT_EQUAL_UINT8(6, loadSubstationConfig().espNowChannel);

  prefs.begin(NVS_NAMESPACE, false);
  prefs.putUInt("espnow_chan", 11);
  prefs.end();
  TEST_ASSERT_EQUAL_UINT8(11, loadSubstationConfig().espNowChannel);
  prefs.begin(NVS_NAMESPACE, false);
}

/** With NVS empty the RS485 loader gives Modbus RTU on the SP3485 pins and the secrets.h substation. */
void test_rs485_loader_defaults() {
  prefs.end();
  Rs485NodeConfig cfg = loadRs485NodeConfig();
  prefs.begin(NVS_NAMESPACE, false);

  const uint8_t expectedMac[6] = SECRET_MAC;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ReaderType::ModbusRtu), static_cast<uint8_t>(cfg.readerType));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedMac, cfg.substation.mac, 6);
  TEST_ASSERT_EQUAL_UINT8(6, cfg.espNow.channel);
  TEST_ASSERT_EQUAL_UINT32(100, cfg.espNow.sendTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(8, cfg.modbus.bus.rx);
  TEST_ASSERT_EQUAL_UINT8(9, cfg.modbus.bus.tx);
  TEST_ASSERT_EQUAL_UINT8(10, cfg.modbus.bus.dere);
  TEST_ASSERT_EQUAL_UINT32(9600, cfg.modbus.bus.baudRate);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.modbus.slaveAddress);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.modbus.pollIntervalMs);
  TEST_ASSERT_EQUAL_UINT8(16, cfg.dlms.clientSap);
  TEST_ASSERT_EQUAL_UINT16(1, cfg.dlms.serverLogical);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.dlms.serverAddrLen);
}

/** Every RS485 key set in NVS overrides its default. */
void test_rs485_loader_reads_nvs() {
  prefs.putUInt("reader", static_cast<uint32_t>(ReaderType::DlmsCosem));
  prefs.putString("sub_mac", "aa:bb:cc:01:02:03");
  prefs.putUInt("espnow_chan", 11);
  prefs.putUInt("baud", 19200);
  prefs.putUInt("slave_addr", 7);
  prefs.putUInt("poll_ms", 30000);
  prefs.putUInt("dlms_client", 32);
  prefs.end();
  Rs485NodeConfig cfg = loadRs485NodeConfig();
  prefs.begin(NVS_NAMESPACE, false);

  const uint8_t expectedMac[6] = {0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ReaderType::DlmsCosem), static_cast<uint8_t>(cfg.readerType));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedMac, cfg.substation.mac, 6);
  TEST_ASSERT_EQUAL_UINT8(11, cfg.espNow.channel);
  TEST_ASSERT_EQUAL_UINT32(19200, cfg.modbus.bus.baudRate);
  TEST_ASSERT_EQUAL_UINT32(19200, cfg.dlms.bus.baudRate);
  TEST_ASSERT_EQUAL_UINT8(7, cfg.modbus.slaveAddress);
  TEST_ASSERT_EQUAL_UINT32(30000, cfg.modbus.pollIntervalMs);
  TEST_ASSERT_EQUAL_UINT8(32, cfg.dlms.clientSap);
}

/** With NVS empty the IR loader gives the simulated head at 300 baud 7E1, inverted. */
void test_ir_loader_defaults() {
  prefs.end();
  IrNodeConfig cfg = loadIrNodeConfig();
  prefs.begin(NVS_NAMESPACE, false);

  const uint8_t expectedMac[6] = SECRET_MAC;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(IrHeadMode::Simulated), static_cast<uint8_t>(cfg.headMode));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedMac, cfg.substation.mac, 6);
  TEST_ASSERT_EQUAL_UINT8(6, cfg.espNow.channel);
  TEST_ASSERT_EQUAL_UINT32(60000, cfg.iec.pollIntervalMs);
  TEST_ASSERT_EQUAL_UINT8(20, cfg.iec.bus.rx);
  TEST_ASSERT_EQUAL_UINT8(21, cfg.iec.bus.tx);
  TEST_ASSERT_EQUAL_UINT32(300, cfg.iec.bus.baudRate);
  TEST_ASSERT_EQUAL_UINT32(SERIAL_7E1, cfg.iec.bus.format);
  TEST_ASSERT_TRUE(cfg.iec.bus.invert);
  TEST_ASSERT_EQUAL_UINT16(5021, cfg.tcp.port);
}

/** Every IR key set in NVS overrides its default. */
void test_ir_loader_reads_nvs() {
  prefs.putUInt("simulate", static_cast<uint32_t>(IrHeadMode::Real));
  prefs.putString("sub_mac", "aa:bb:cc:01:02:03");
  prefs.putUInt("espnow_chan", 11);
  prefs.putUInt("poll_ms", 15000);
  prefs.putUInt("ir_invert", 0);
  prefs.end();
  IrNodeConfig cfg = loadIrNodeConfig();
  prefs.begin(NVS_NAMESPACE, false);

  const uint8_t expectedMac[6] = {0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(IrHeadMode::Real), static_cast<uint8_t>(cfg.headMode));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedMac, cfg.substation.mac, 6);
  TEST_ASSERT_EQUAL_UINT8(11, cfg.espNow.channel);
  TEST_ASSERT_EQUAL_UINT32(15000, cfg.iec.pollIntervalMs);
  TEST_ASSERT_FALSE(cfg.iec.bus.invert);
}

/** With NVS empty the CV loader gives the secrets.h camera and the /json path. */
void test_cv_loader_defaults() {
  prefs.end();
  CvNodeConfig cfg = loadCvNodeConfig();
  prefs.begin(NVS_NAMESPACE, false);

  const uint8_t expectedMac[6] = SECRET_MAC;
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedMac, cfg.substation.mac, 6);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.espNow.channel);
  TEST_ASSERT_EQUAL_UINT32(200, cfg.espNow.sendTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(30000, cfg.cam.pollIntervalMs);
  TEST_ASSERT_EQUAL_STRING(SECRET_CAM_HOST, cfg.cam.host);
  TEST_ASSERT_EQUAL_STRING("/json", cfg.cam.path);
  TEST_ASSERT_EQUAL_STRING(SECRET_CAM_FLOW_NAME, cfg.cam.flowName);
}

/** Every CV key set in NVS overrides its default. */
void test_cv_loader_reads_nvs() {
  prefs.putString("sub_mac", "aa:bb:cc:01:02:03");
  prefs.putUInt("espnow_chan", 11);
  prefs.putUInt("poll_ms", 10000);
  prefs.putString("cam_host", "10.0.0.5");
  prefs.putString("cam_flow", "meter");
  prefs.end();
  CvNodeConfig cfg = loadCvNodeConfig();
  prefs.begin(NVS_NAMESPACE, false);

  const uint8_t expectedMac[6] = {0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedMac, cfg.substation.mac, 6);
  TEST_ASSERT_EQUAL_UINT8(11, cfg.espNow.channel);
  TEST_ASSERT_EQUAL_UINT32(10000, cfg.cam.pollIntervalMs);
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", cfg.cam.host);
  TEST_ASSERT_EQUAL_STRING("meter", cfg.cam.flowName);
}

/** With NVS empty the gateway loader enables TLS MQTT to the secrets.h broker, with 3 LoRa tries of 1500 ms. */
void test_gateway_loader_defaults() {
  prefs.end();
  GatewayConfig cfg = loadGatewayConfig();
  prefs.begin(NVS_NAMESPACE, false);

  TEST_ASSERT_TRUE(cfg.mqtt.enabled);
  TEST_ASSERT_EQUAL_STRING(SECRET_MQTT_SERVER, cfg.mqtt.server);
  TEST_ASSERT_EQUAL_UINT16(SECRET_MQTT_PORT, cfg.mqtt.port);
  TEST_ASSERT_EQUAL_STRING(SECRET_MQTT_TOPIC, cfg.mqtt.topic);
  TEST_ASSERT_EQUAL_STRING(SECRET_WIFI_SSID, cfg.wifi.ssid);
  TEST_ASSERT_EQUAL_UINT8(3, cfg.link.maxRetries);
  TEST_ASSERT_EQUAL_UINT32(1500, cfg.link.ackTimeoutMs);
}

/** Every gateway key set in NVS overrides its default, including turning MQTT off. */
void test_gateway_loader_reads_nvs() {
  prefs.putUInt("mqtt_on", 0);
  prefs.putString("mqtt_host", "abc123.s1.eu.hivemq.cloud");
  prefs.putUInt("mqtt_port", 8884);
  prefs.putString("mqtt_topic", "kaizen/test");
  prefs.putString("wifi_ssid", "Bench");
  prefs.putUInt("lora_retries", 5);
  prefs.putUInt("lora_ack_ms", 2500);
  prefs.end();
  GatewayConfig cfg = loadGatewayConfig();
  prefs.begin(NVS_NAMESPACE, false);

  TEST_ASSERT_FALSE(cfg.mqtt.enabled);
  TEST_ASSERT_EQUAL_STRING("abc123.s1.eu.hivemq.cloud", cfg.mqtt.server);
  TEST_ASSERT_EQUAL_UINT16(8884, cfg.mqtt.port);
  TEST_ASSERT_EQUAL_STRING("kaizen/test", cfg.mqtt.topic);
  TEST_ASSERT_EQUAL_STRING("Bench", cfg.wifi.ssid);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.link.maxRetries);
  TEST_ASSERT_EQUAL_UINT32(2500, cfg.link.ackTimeoutMs);
}

/** On a fresh board the counter starts at 1, and each value is saved. */
void test_seq_starts_at_one_and_saves() {
  seqCounterBegin();
  TEST_ASSERT_EQUAL_UINT32(1, seqNext());
  TEST_ASSERT_EQUAL_UINT32(2, seqNext());
  TEST_ASSERT_EQUAL_UINT32(2, storedSeq());
}

/** Reloading picks up the value stored in NVS, as after a reboot. */
void test_seq_resumes_from_nvs() {
  storeSeq(41);
  seqCounterBegin();
  TEST_ASSERT_EQUAL_UINT32(42, seqNext());
  TEST_ASSERT_EQUAL_UINT32(42, storedSeq());
}

/** @} */

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_read_u32_written_key);
  RUN_TEST(test_read_u32_unset_key_gives_default);
  RUN_TEST(test_read_str_written_key);
  RUN_TEST(test_read_str_unset_key_gives_default);
  RUN_TEST(test_read_str_truncates);
  RUN_TEST(test_read_mac_written_key);
  RUN_TEST(test_read_mac_falls_back);
  RUN_TEST(test_substation_loader_reads_nvs);
  RUN_TEST(test_rs485_loader_defaults);
  RUN_TEST(test_rs485_loader_reads_nvs);
  RUN_TEST(test_ir_loader_defaults);
  RUN_TEST(test_ir_loader_reads_nvs);
  RUN_TEST(test_cv_loader_defaults);
  RUN_TEST(test_cv_loader_reads_nvs);
  RUN_TEST(test_gateway_loader_defaults);
  RUN_TEST(test_gateway_loader_reads_nvs);
  RUN_TEST(test_seq_starts_at_one_and_saves);
  RUN_TEST(test_seq_resumes_from_nvs);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
