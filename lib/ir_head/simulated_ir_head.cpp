#include "simulated_ir_head.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>

// Canned responses matching what a real EM211 would send, per IEC 62056-21
// mode C.
//
// Identification message shape: "/" + 3-char manufacturer ID + 1-char
// baud-rate ID + identification text + CRLF. The baud-rate ID here is '5',
// which Table 6 of the standard maps to 9600 -- deliberately different from
// the 19200 this codebase used to hardcode, so a correct parser and a
// broken one produce visibly different negotiated baud rates.
static const char ID_RESPONSE[] = "/EMH5EM211\r\n";
static const char DATA_BLOCK[] =
    "1-0:1.8.0(001234.567*kWh)\r\n"
    "1-0:2.8.0(000045.123*kWh)\r\n"
    "!\r\n";

void SimulatedIrHead::queueResponse(const char *response) {
  pendingResponse = response;
  pendingLen = strlen(response);
  pendingPos = 0;
}

void SimulatedIrHead::init() {
  state = AWAITING_REQUEST;
  pendingResponse = nullptr;
}

int SimulatedIrHead::send(const uint8_t *data, size_t len) {
  // Look at what the reader just sent to decide what a real meter would
  // send back next.
  if (state == AWAITING_REQUEST && len == 5 &&
      memcmp(data, "/?!\r\n", 5) == 0) {
    queueResponse(ID_RESPONSE);
    state = SENDING_ID;
  } else if (state == AWAITING_ACK && len >= 1 && data[0] == 0x06) {
    queueResponse(DATA_BLOCK);
    state = SENDING_DATA;
  }
  return EXIT_SUCCESS;
}

int SimulatedIrHead::readByte() {
  if (!available()) return -1;

  uint8_t byte = pendingResponse[pendingPos++];

  if (pendingPos >= pendingLen) {
    if (state == SENDING_ID) state = AWAITING_ACK;
    else if (state == SENDING_DATA) state = DONE;
    pendingResponse = nullptr;
  }
  return byte;
}

bool SimulatedIrHead::available() {
  return pendingResponse != nullptr && pendingPos < pendingLen;
}

void SimulatedIrHead::setBaudRate(uint32_t baud) {
  // No real link to reconfigure -- just log it so the simulated run looks
  // like the real handshake in the serial monitor.
  Serial.printf("[SimulatedIrHead] would switch to %u baud\n", baud);
}
