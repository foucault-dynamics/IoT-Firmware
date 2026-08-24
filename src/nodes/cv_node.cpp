#include <Arduino.h>
#include "nodes.h"

// CV module node. Intended composition: a UART Module plus Esp32CamReader
// (lib/esp32cam) polling the ESP32-CAM over the link defined in
// lib/shared/cam_link_protocol.h, which is still TODO.

void cvNodeSetup() {
  Serial.println("[CV] CV node: not implemented.");
}

void cvNodeLoop() {
}
