/**
 * @file
 * Tests for the NVS config readers, a loader and the sequence counter, run in
 * QEMU against its emulated flash.
 *
 * Every run boots a freshly merged flash image, so NVS starts empty. setUp()
 * also wipes both namespaces, so each test starts from defaults.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <unity.h>

#include "nvs_config.h"
#include "nvs_read.h"
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
  RUN_TEST(test_seq_starts_at_one_and_saves);
  RUN_TEST(test_seq_resumes_from_nvs);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
