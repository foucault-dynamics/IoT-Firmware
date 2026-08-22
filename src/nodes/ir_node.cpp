#include <Arduino.h>
#include "nodes.h"

// IR reader node. The working IR stack (an IrHead Module over Serial1 at
// SERIAL_7E1 plus an IEC 62056-21 reader with OBIS parsing) lives on the
// ir-module branch and plugs in here when ported. See lib/ir_head/ir_head.h
// for the intended composition.

void irNodeSetup() {
  Serial.println("[IR] IR node: not implemented on this branch (see ir-module branch).");
}

void irNodeLoop() {
}
