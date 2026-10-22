/**
 * @file
 * Tests for MeterGate over a scripted FakeReader, run in QEMU.
 *
 * Covers when the gate probes the meter while not ready, that it stays ready
 * without further probes, and how failed reads drop it back to not ready.
 * The retry interval is cut to RETRY_MS so the waits stay short. millis() runs
 * in real time under QEMU, so the waits are real.
 */

#include <Arduino.h>
#include <unity.h>

#include "../support/fake_reader.h"
#include "end_node_bringup.h"

namespace {

/** Retry interval the tests run the gate with, in ms. */
constexpr uint32_t RETRY_MS = 50;

/**
 * Makes @p gate ready with one passing probe.
 *
 * @param[in] gate    Gate to make ready.
 * @param[in] reader  Its reader.
 */
void makeReady(MeterGate &gate, FakeReader &reader) {
  reader.queueProbe(EXIT_SUCCESS);
  TEST_ASSERT_TRUE(gate.ready());
}

}  // namespace

/** Nothing to set up. */
void setUp() {}

/** Nothing to clean up. */
void tearDown() {}

/**
 * @defgroup test_qemu_end_node_bringup End node bring up
 * @ingroup tests
 * Tests in test_qemu_end_node_bringup/test_main.cpp.
 * @{
 */

/** The first ready() probes, and a pass makes the gate ready. */
void test_first_ready_probes() {
  FakeReader reader;
  MeterGate gate("test", reader, RETRY_MS);
  reader.queueProbe(EXIT_SUCCESS);
  TEST_ASSERT_TRUE(gate.ready());
  TEST_ASSERT_EQUAL_INT(1, reader.probes);
}

/** After a failed probe, no probe runs inside the retry gap, and one runs after it. */
void test_reprobes_only_after_gap() {
  FakeReader reader;
  MeterGate gate("test", reader, RETRY_MS);
  reader.queueProbe(EXIT_FAILURE);
  TEST_ASSERT_FALSE(gate.ready());
  TEST_ASSERT_FALSE(gate.ready());
  TEST_ASSERT_EQUAL_INT(1, reader.probes);

  delay(RETRY_MS + 10);
  reader.queueProbe(EXIT_SUCCESS);
  TEST_ASSERT_TRUE(gate.ready());
  TEST_ASSERT_EQUAL_INT(2, reader.probes);
}

/** A ready gate stays ready with no further probes. */
void test_stays_ready_without_probing() {
  FakeReader reader;
  MeterGate gate("test", reader, RETRY_MS);
  makeReady(gate, reader);
  delay(RETRY_MS + 10);
  TEST_ASSERT_TRUE(gate.ready());
  TEST_ASSERT_TRUE(gate.ready());
  TEST_ASSERT_EQUAL_INT(1, reader.probes);
}

/** Two failed reads then a success stay ready, and the success resets the count. */
void test_success_resets_failures() {
  FakeReader reader;
  MeterGate gate("test", reader, RETRY_MS);
  makeReady(gate, reader);
  gate.reportRead(EXIT_FAILURE);
  gate.reportRead(EXIT_FAILURE);
  gate.reportRead(EXIT_SUCCESS);
  TEST_ASSERT_TRUE(gate.ready());

  // Had the count not reset, the first of these would lose the meter
  gate.reportRead(EXIT_FAILURE);
  gate.reportRead(EXIT_FAILURE);
  TEST_ASSERT_TRUE(gate.ready());
  TEST_ASSERT_EQUAL_INT(1, reader.probes);
}

/** Three failed reads in a row lose the meter, and the next ready() probes straight away. */
void test_three_failures_fall_back() {
  FakeReader reader;
  MeterGate gate("test", reader, RETRY_MS);
  makeReady(gate, reader);
  gate.reportRead(EXIT_FAILURE);
  gate.reportRead(EXIT_FAILURE);
  gate.reportRead(EXIT_FAILURE);

  // Well inside the retry gap of the first probe, so only the fallback explains a probe
  reader.queueProbe(EXIT_FAILURE);
  TEST_ASSERT_FALSE(gate.ready());
  TEST_ASSERT_EQUAL_INT(2, reader.probes);
}

/** After falling back, a passing probe makes the gate ready again. */
void test_ready_again_after_pass() {
  FakeReader reader;
  MeterGate gate("test", reader, RETRY_MS);
  makeReady(gate, reader);
  for (uint8_t i = 0; i < READS_BEFORE_LOST; i++) {
    gate.reportRead(EXIT_FAILURE);
  }
  reader.queueProbe(EXIT_SUCCESS);
  TEST_ASSERT_TRUE(gate.ready());
  TEST_ASSERT_EQUAL_INT(2, reader.probes);
}

/** reportRead() does nothing while not ready, so it cannot force an early probe. */
void test_report_ignored_while_not_ready() {
  FakeReader reader;
  MeterGate gate("test", reader, RETRY_MS);
  reader.queueProbe(EXIT_FAILURE);
  TEST_ASSERT_FALSE(gate.ready());
  for (uint8_t i = 0; i < READS_BEFORE_LOST; i++) {
    gate.reportRead(EXIT_FAILURE);
  }
  gate.reportRead(EXIT_SUCCESS);
  TEST_ASSERT_FALSE(gate.ready());
  TEST_ASSERT_EQUAL_INT(1, reader.probes);
}

/** @} */

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_first_ready_probes);
  RUN_TEST(test_reprobes_only_after_gap);
  RUN_TEST(test_stays_ready_without_probing);
  RUN_TEST(test_success_resets_failures);
  RUN_TEST(test_three_failures_fall_back);
  RUN_TEST(test_ready_again_after_pass);
  RUN_TEST(test_report_ignored_while_not_ready);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}
