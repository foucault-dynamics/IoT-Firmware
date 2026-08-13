#ifndef ESP32CAM_H
#define ESP32CAM_H

#include "shared_payload.h"
#include "uart.h"

/*
 * C3-side reader for the ESP32-CAM head.
 *
 * Parses the messages defined in cam_link_protocol.h, which is shared
 * with the CAM board's own firmware so the format is defined once.
 */
class Esp32CamReader {
 private:
  Uart &stream;

 public:
  // Constructor
  explicit Esp32CamReader(Uart &stream);
  // Setup
  int setup();
  // Receive payload through stream
  int poll(Payload &out);
};

#endif
