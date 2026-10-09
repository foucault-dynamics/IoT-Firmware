/**
 * @file
 * Reading sequence number used by the meter nodes, persisted across reboots.
 *
 * Kept in its own NVS namespace, "runtime", so the `clear` serial command,
 * which wipes the "config" namespace, never resets it.
 */

#pragma once

#include <cstdint>

/** Loads the last sequence number from NVS. Call once in setup(). */
void seqCounterBegin();

/**
 * Increments the sequence number and saves it to NVS.
 *
 * Wraps to 0 after UINT32_MAX. At one reading a second that takes about 136
 * years, so nothing handles it.
 *
 * @return The new sequence number.
 */
uint32_t seqNext();
