#include "seq_counter.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

const char *NVS_NAMESPACE = "runtime";
const char *SEQ_KEY = "seq";

Preferences prefs;
uint32_t seq = 0;

}  // namespace

void seqCounterBegin() {
  prefs.begin(NVS_NAMESPACE, false);
  seq = prefs.getUInt(SEQ_KEY, 0);
  Serial.printf("[CFG] %s = %lu (NVS)\n", SEQ_KEY, static_cast<unsigned long>(seq));
}

uint32_t seqNext() {
  seq++;
  prefs.putUInt(SEQ_KEY, seq);
  return seq;
}
