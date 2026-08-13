#ifndef SIMULATED_IR_HEAD_H
#define SIMULATED_IR_HEAD_H

#include "ir_head.h"
#include <cstddef>

/*
 * Stands in for the real IR hardware, which doesn't exist yet (the EE
 * team's UART-to-IR circuit isn't built). Plays back the same bytes a
 * real EM211 would send over its optical port, in response to whatever
 * the reader sends, so Iec6205621Reader can be built and tested
 * end-to-end now and pointed at RealIrHead unchanged once the hardware
 * lands.
 */
class SimulatedIrHead : public IrHead {
 private:
  enum State { AWAITING_REQUEST, SENDING_ID, AWAITING_ACK, SENDING_DATA, DONE };
  State state = AWAITING_REQUEST;

  const char *pendingResponse = nullptr;
  size_t pendingLen = 0;
  size_t pendingPos = 0;

  void queueResponse(const char *response);

 public:
  int setup() override;
  int send(const uint8_t *data, size_t len) override;
  int receive(uint8_t *buf, size_t maxLen) override;
  bool available() override;
  void setBaudRate(uint32_t baud) override;
};

#endif
