/**
 * @file
 * The substation's store and forward buffer for readings awaiting a gateway ACK.
 *
 * Keeps a ring of readings per meter node, keyed by UID, so a reading is held
 * until the gateway acknowledges it. Use it as peek, send, then pop on
 * success, so a failed send leaves the reading in place for a retry. Peeking
 * rotates across nodes so one busy node cannot starve the others.
 */

#pragma once

#include <cstddef>

#include "shared_payload.h"

/**
 * Stores a reading under its node's UID.
 *
 * A new UID claims a free node slot. When that node's ring is full, its
 * oldest reading is overwritten.
 *
 * @param[in] p  Reading to copy in.
 * @retval true   Stored.
 * @retval false  The UID is new and every node slot is taken. Dropped.
 */
bool readingBufferPush(const Payload &p);

/**
 * Copies out the oldest reading of the next node in turn, without removing it.
 *
 * @param[out] out  The reading. Untouched if the buffer is empty.
 * @retval true   @p out holds a reading. Call readingBufferPop() once it is
 *                delivered.
 * @retval false  The buffer is empty.
 */
bool readingBufferPeek(Payload &out);

/**
 * Removes the reading returned by the last readingBufferPeek().
 *
 * Moves on to the next node, so the next peek serves a different one. Does
 * nothing if there was no peek since the last pop.
 */
void readingBufferPop();

/**
 * Counts every reading held, across all nodes.
 *
 * @return Number of buffered readings.
 */
size_t readingBufferCount();

/**
 * Empties the buffer and frees every node slot.
 *
 * Also forgets the last peek and restarts the node rotation from the first
 * slot, leaving the buffer as it was at boot.
 */
void readingBufferClear();
