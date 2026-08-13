#include "simulated_ir_head.h"
#include <Arduino.h>
#include <cstring>

// Canned responses matching what a real EM211 would send, per IEC 62056-21
// mode C.
static const char ID_RESPONSE[] = "/EM5\\300EM211\r\n";
static const char DATA_BLOCK[] =
    "1-0:1.8.0(001234.567*kWh)\r\n"
    "1-0:2.8.0(000045.123*kWh)\r\n"
    "!\r\n";

void SimulatedIrHead::queueResponse(const char *response) {
  pendingResponse = response;
  pendingLen = strlen(response);
  pendingPos = 0;
}

int SimulatedIrHead::setup() {
  state = AWAITING_REQUEST;
  pendingResponse = nullptr;
  return 0;
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
  return len;
}

int SimulatedIrHead::receive(uint8_t *buf, size_t maxLen) {
  if (!pendingResponse) return 0;

  size_t n = 0;
  while (n < maxLen && pendingPos < pendingLen) {
    buf[n++] = pendingResponse[pendingPos++];
  }

  if (pendingPos >= pendingLen) {
    if (state == SENDING_ID) state = AWAITING_ACK;
    else if (state == SENDING_DATA) state = DONE;
    pendingResponse = nullptr;
  }
  return n;
}

bool SimulatedIrHead::available() {
  return pendingResponse != nullptr && pendingPos < pendingLen;
}

void SimulatedIrHead::setBaudRate(uint32_t baud) {
  // No real link to reconfigure -- just log it so the simulated run looks
  // like the real handshake in the serial monitor.
  Serial.printf("[SimulatedIrHead] would switch to %u baud\n", baud);
}
