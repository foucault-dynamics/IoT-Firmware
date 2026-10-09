/**
 * @file
 * Unit tests for the substation's reading buffer, run in QEMU.
 *
 * test_peek_overflow_pop_keeps_unsent_reading and
 * test_drained_slot_is_freed_for_new_uid guard against two fixed bugs.
 */

#include <Arduino.h>
#include <unity.h>

#include "reading_buffer.h"

namespace {

/** Readings held per node, matching READINGS_PER_NODE in reading_buffer.cpp. */
constexpr uint32_t RING_SIZE = 288;
/** Node slots, matching MAX_END_NODES in reading_buffer.cpp. */
constexpr uint8_t SLOT_COUNT = 8;

/**
 * Builds a payload for a test node.
 *
 * @param[in] node  Written to the UID's first byte, the rest is zero.
 * @param[in] seq   Sequence number. Also sets kwh_import to seq / 10.
 * @return The payload, with community_id 1 and unit_id 2.
 */
Payload makePayload(uint8_t node, uint32_t seq) {
  Payload p = {};
  p.uid[0] = node;
  p.seq = seq;
  p.kwh_import = seq / 10.0f;
  p.kwh_export = 0.5f;
  p.voltage = 230.0f;
  p.community_id = 1;
  p.unit_id = 2;
  return p;
}

/**
 * Peeks and pops one reading, failing the test if the buffer is empty.
 *
 * @return The reading that was removed.
 */
Payload takeOne() {
  Payload out = {};
  TEST_ASSERT_TRUE_MESSAGE(readingBufferPeek(out), "buffer unexpectedly empty");
  readingBufferPop();
  return out;
}

}  // namespace

/** Resets the buffer before every test. */
void setUp() {
  readingBufferClear();
}

/** Nothing to clean up. */
void tearDown() {}

/** Readings from one node come out in the order they went in. */
void test_fifo_within_one_node() {
  for (uint32_t seq = 1; seq <= 5; seq++) {
    TEST_ASSERT_TRUE(readingBufferPush(makePayload(1, seq)));
  }
  for (uint32_t seq = 1; seq <= 5; seq++) {
    Payload out = takeOne();
    TEST_ASSERT_EQUAL_UINT32(seq, out.seq);
    TEST_ASSERT_EQUAL_FLOAT(seq / 10.0f, out.kwh_import);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, out.kwh_export);
    TEST_ASSERT_EQUAL_FLOAT(230.0f, out.voltage);
    TEST_ASSERT_EQUAL_UINT8(1, out.uid[0]);
  }
  TEST_ASSERT_EQUAL_size_t(0, readingBufferCount());
}

/** Peeking an empty buffer returns false and leaves the output untouched. */
void test_peek_empty_leaves_out_untouched() {
  Payload out;
  memset(&out, 0xAB, sizeof(out));
  Payload before = out;
  TEST_ASSERT_FALSE(readingBufferPeek(out));
  TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(out));
}

/** Popping with no prior peek, or on an empty buffer, changes nothing. */
void test_pop_without_peek_does_nothing() {
  readingBufferPop();
  TEST_ASSERT_EQUAL_size_t(0, readingBufferCount());

  readingBufferPush(makePayload(1, 1));
  readingBufferPush(makePayload(1, 2));
  readingBufferPop();
  TEST_ASSERT_EQUAL_size_t(2, readingBufferCount());

  // A second pop after one peek must not remove a second reading
  takeOne();
  readingBufferPop();
  TEST_ASSERT_EQUAL_size_t(1, readingBufferCount());
}

/** A full ring overwrites its oldest reading and stays at capacity. */
void test_full_ring_overwrites_oldest() {
  for (uint32_t seq = 1; seq <= RING_SIZE + 1; seq++) {
    TEST_ASSERT_TRUE(readingBufferPush(makePayload(1, seq)));
  }
  TEST_ASSERT_EQUAL_size_t(RING_SIZE, readingBufferCount());

  Payload out = {};
  TEST_ASSERT_TRUE(readingBufferPeek(out));
  TEST_ASSERT_EQUAL_UINT32(2, out.seq);
}

/** With every slot taken, a new UID is rejected. */
void test_ninth_uid_rejected() {
  for (uint8_t node = 1; node <= SLOT_COUNT; node++) {
    TEST_ASSERT_TRUE(readingBufferPush(makePayload(node, 1)));
  }
  TEST_ASSERT_FALSE(readingBufferPush(makePayload(SLOT_COUNT + 1, 1)));
  TEST_ASSERT_EQUAL_size_t(SLOT_COUNT, readingBufferCount());
}

/** After a pop, the next peek serves the next node, so none is starved. */
void test_pop_rotates_to_next_node() {
  for (uint32_t seq = 1; seq <= 3; seq++) {
    readingBufferPush(makePayload(1, seq));
    readingBufferPush(makePayload(2, seq));
  }
  TEST_ASSERT_EQUAL_UINT8(1, takeOne().uid[0]);
  TEST_ASSERT_EQUAL_UINT8(2, takeOne().uid[0]);
  TEST_ASSERT_EQUAL_UINT8(1, takeOne().uid[0]);
  TEST_ASSERT_EQUAL_UINT8(2, takeOne().uid[0]);
}

/** A peek with no pop, i.e. a failed send, returns the same reading again. */
void test_peek_without_pop_repeats() {
  readingBufferPush(makePayload(1, 1));
  readingBufferPush(makePayload(1, 2));
  Payload first = {};
  Payload second = {};
  TEST_ASSERT_TRUE(readingBufferPeek(first));
  TEST_ASSERT_TRUE(readingBufferPeek(second));
  TEST_ASSERT_EQUAL_UINT32(1, first.seq);
  TEST_ASSERT_EQUAL_MEMORY(&first, &second, sizeof(Payload));
  TEST_ASSERT_EQUAL_size_t(2, readingBufferCount());
}

/** community_id and unit_id come from the latest push for that UID. */
void test_ids_follow_latest_push() {
  readingBufferPush(makePayload(1, 1));
  Payload p = makePayload(1, 2);
  p.community_id = 7;
  p.unit_id = 9;
  readingBufferPush(p);

  Payload out = {};
  TEST_ASSERT_TRUE(readingBufferPeek(out));
  TEST_ASSERT_EQUAL_UINT32(1, out.seq);
  TEST_ASSERT_EQUAL_UINT8(7, out.community_id);
  TEST_ASSERT_EQUAL_UINT8(9, out.unit_id);
}

/**
 * A push between a peek and its pop must not cost an unsent reading.
 *
 * Regression: the push overwrote the peeked reading A, then the pop removed
 * B, which was never sent.
 */
void test_peek_overflow_pop_keeps_unsent_reading() {
  for (uint32_t seq = 1; seq <= RING_SIZE; seq++) {
    readingBufferPush(makePayload(1, seq));
  }
  Payload out = {};
  TEST_ASSERT_TRUE(readingBufferPeek(out));
  TEST_ASSERT_EQUAL_UINT32(1, out.seq);

  readingBufferPush(makePayload(1, RING_SIZE + 1));
  readingBufferPop();

  TEST_ASSERT_TRUE(readingBufferPeek(out));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, out.seq, "pop removed an unsent reading instead of the peeked one");
}

/**
 * A node slot is freed once its ring is drained, so a new UID can claim it.
 *
 * Regression: NodeSlot::used was never cleared.
 */
void test_drained_slot_is_freed_for_new_uid() {
  for (uint8_t node = 1; node <= SLOT_COUNT; node++) {
    readingBufferPush(makePayload(node, 1));
  }
  // Rotation starts at slot 0, which belongs to node 1
  TEST_ASSERT_EQUAL_UINT8(1, takeOne().uid[0]);

  TEST_ASSERT_TRUE_MESSAGE(readingBufferPush(makePayload(SLOT_COUNT + 1, 1)), "drained slot was not freed for a new UID");
}

/** Runs every test once the serial port is up. */
void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_fifo_within_one_node);
  RUN_TEST(test_peek_empty_leaves_out_untouched);
  RUN_TEST(test_pop_without_peek_does_nothing);
  RUN_TEST(test_full_ring_overwrites_oldest);
  RUN_TEST(test_ninth_uid_rejected);
  RUN_TEST(test_pop_rotates_to_next_node);
  RUN_TEST(test_peek_without_pop_repeats);
  RUN_TEST(test_ids_follow_latest_push);
  RUN_TEST(test_peek_overflow_pop_keeps_unsent_reading);
  RUN_TEST(test_drained_slot_is_freed_for_new_uid);
  UNITY_END();
}

/** Unused, every test runs in setup(). */
void loop() {}