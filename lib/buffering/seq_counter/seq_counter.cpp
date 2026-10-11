/**
 * @file
 * Sequence counter implementation.
 */

#include "seq_counter.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstdlib>

namespace {

const char *NVS_NAMESPACE = "runtime";  ///< NVS namespace, separate from "config".
const char *SEQ_KEY = "seq";            ///< NVS key holding the counter.

Preferences prefs;  ///< Left open for the life of the firmware.
uint32_t seq = 0;   ///< Last sequence number handed out.

}  // namespace

int seqCounterBegin() {
  // Close first, so a second call reopens rather than fails on a started handle
  prefs.end();
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return EXIT_FAILURE;
  }
  seq = prefs.getUInt(SEQ_KEY, 0);
  Serial.printf("[CFG] %s = %lu (NVS)\n", SEQ_KEY, static_cast<unsigned long>(seq));
  return EXIT_SUCCESS;
}

uint32_t seqNext() {
  seq++;
  prefs.putUInt(SEQ_KEY, seq);
  return seq;
}
