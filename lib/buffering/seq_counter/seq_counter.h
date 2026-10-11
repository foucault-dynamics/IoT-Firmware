/**
 * @file
 * Reading sequence number used by the meter nodes, persisted across reboots.
 *
 * Kept in its own NVS namespace, "runtime", so the `clear` serial command,
 * which wipes the "config" namespace, never resets it.
 */

#pragma once

#include <cstdint>

/**
 * Opens the counter's NVS namespace and loads the last sequence number. Call
 * once in setup().
 *
 * @retval EXIT_SUCCESS  Loaded, or 0 on a fresh board.
 * @retval EXIT_FAILURE  NVS could not be opened. seqNext() must not be called,
 *                       since its numbers would neither persist nor resume.
 */
int seqCounterBegin();

/**
 * Increments the sequence number and saves it to NVS.
 *
 * Wraps to 0 after UINT32_MAX. At one reading a second that takes about 136
 * years, so nothing handles it.
 *
 * @return The new sequence number.
 */
uint32_t seqNext();
