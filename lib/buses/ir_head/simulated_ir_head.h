/**
 * @file
 * SimulatedIrHead, a software IrHead that plays back an EM211 meter.
 */

#ifndef SIMULATED_IR_HEAD_H
#define SIMULATED_IR_HEAD_H

#include "ir_head.h"
#include <cstddef>

/**
 * Stands in for the real IR hardware, which does not exist yet.
 *
 * Plays back the same bytes a real EM211 would send over its optical port, in
 * response to whatever the reader sends. This lets Iec6205621Reader be built
 * and tested end to end now, and pointed at RealIrHead unchanged once the
 * hardware lands.
 */
class SimulatedIrHead : public IrHead {
 private:
  /** Where the simulated meter is in the IEC 62056-21 session. */
  enum State {
    AWAITING_REQUEST,  ///< Idle, waiting for the "/?!" request message.
    SENDING_ID,        ///< Playing back the identification message.
    AWAITING_ACK,      ///< Waiting for the ACK that selects the baud rate.
    SENDING_DATA,      ///< Playing back the data block.
    DONE               ///< Session finished. The next request starts over.
  };
  State state = AWAITING_REQUEST;  ///< Current session step.

  const char *pendingResponse = nullptr;  ///< Response being played back, or nullptr.
  size_t pendingLen = 0;                  ///< Length of #pendingResponse.
  size_t pendingPos = 0;                  ///< Next byte of #pendingResponse to hand out.

  /**
   * Starts playing back a canned response through readByte().
   *
   * @param[in] response  Null terminated response. Must be static.
   */
  void queueResponse(const char *response);

 public:
  /**
   * Resets the simulated meter to idle.
   *
   * @retval EXIT_SUCCESS  Always.
   */
  int init() override;

  /**
   * Inspects what the reader sent and queues the meter's reply.
   *
   * The request message queues the identification message from any state, as
   * on a real meter, and an ACK queues the data block. Anything else is
   * ignored.
   *
   * @param[in] data  Bytes the reader sent.
   * @param[in] len   Number of bytes in @p data.
   * @retval EXIT_SUCCESS  Always.
   */
  int send(const uint8_t *data, size_t len) override;

  int readByte() override;
  bool available() override;

  /**
   * Logs the switch, since there is no real link to retune.
   *
   * @param[in] baud  Rate the reader negotiated, in baud.
   */
  void setBaudRate(uint32_t baud) override;
};

#endif
