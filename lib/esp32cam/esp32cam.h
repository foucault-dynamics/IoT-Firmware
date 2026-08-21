#ifndef ESP32CAM_H
#define ESP32CAM_H

#include "module.h"
#include "shared_payload.h"

/*
 * C3-side reader for the ESP32-CAM head.
 *
 * Parses the messages defined in cam_link_protocol.h, which is shared
 * with the CAM board's own firmware so the format is defined once.
 */
class Esp32CamReader {
 private:
  Module &stream;

 public:
  // Constructor
  explicit Esp32CamReader(Module &stream);
  // Setup
  int setup();
  // Receive payload through stream
  int poll(Payload &out);
};

#endif
