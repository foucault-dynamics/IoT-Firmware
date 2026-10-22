/**
 * @file
 * LoRaLink, the Transmitter that adds ACKs and retries on top of LoRaModule.
 */

#ifndef LORA_LINK_H
#define LORA_LINK_H

#include "loramodule.h"
#include "networking_config.h"
#include "transmitter.h"

#include <cstddef>
#include <cstdint>

#include "shared_payload.h"

/** Progress of the send started by LoRaLink::beginSend(). */
enum class LoRaSendStatus : uint8_t {
  Idle,     ///< No send in flight.
  Pending,  ///< Still transmitting, waiting for the ACK or backing off.
  Acked,    ///< The matching ACK arrived. The link is idle again.
  Failed,   ///< Every attempt went unacknowledged. The link is idle again.
};

/**
 * Payload and ACK protocol with retries, on top of a LoRaModule.
 *
 * LoRa has no delivery confirmation built into the radio the way ESP-NOW
 * does, so this layer supplies its own. A send retries until an AckPayload
 * keyed on the sent Payload's UID and sequence number comes back, or
 * LoRaLinkConfig::maxRetries is used up. receivePacket() replies to every
 * Payload with that ACK.
 *
 * A send runs either blocking through sendPacket(), or in steps through
 * beginSend() and pollSend(), so a caller with other work (the substation
 * beaconing and answering its nodes) is not held up for the whole retry
 * scheme.
 *
 * The address is ignored on both sides, because LoRa is a single shared
 * broadcast channel here, not addressed like ESP-NOW.
 */
class LoRaLink : public Transmitter {
 private:
  /** Where the send in flight is in its current attempt. */
  enum class SendPhase : uint8_t {
    Idle,      ///< No send in flight.
    Transmit,  ///< The next attempt is due to go out.
    AwaitAck,  ///< Sent, listening for the ACK.
    Backoff,   ///< Attempt failed, waiting SEND_BACKOFF_MS before the next.
  };

  /** Wait after a failed attempt before the next one, in ms. */
  static constexpr unsigned long SEND_BACKOFF_MS = 500;

  LoRaModule &radio;          ///< Radio the link runs over.
  LoRaLinkConfig config;      ///< Retry count and ACK timeout.
  Payload pending;            ///< Reading being sent, valid unless #phase is Idle.
  uint8_t attempt;            ///< Attempts made for #pending so far.
  SendPhase phase;            ///< Where the send in flight is.
  unsigned long phaseStart;   ///< millis() at which #phase was entered.

  /**
   * Moves to @p next and restarts the phase timer.
   *
   * @param[in] next  Phase to enter.
   */
  void enterPhase(SendPhase next);

  /**
   * Reads exactly @p len bytes of the current packet.
   *
   * Stops when the radio runs dry, so a short packet fails the read instead
   * of looping forever.
   *
   * @param[out] buf  Destination, at least @p len bytes.
   * @param[in]  len  Bytes to read.
   * @retval true   Read all @p len bytes.
   * @retval false  The packet ran out first.
   */
  bool readFrame(uint8_t *buf, size_t len);

 public:
  /**
   * Stores the radio and config. Nothing starts until init().
   *
   * @param[in] radio   Radio to run over. Must outlive the link.
   * @param[in] config  Retry count and ACK timeout, copied.
   */
  LoRaLink(LoRaModule &radio, const LoRaLinkConfig &config);

  /**
   * Starts the radio.
   *
   * @retval EXIT_SUCCESS  Radio ready.
   * @retval EXIT_FAILURE  LoRaModule::init() failed.
   */
  int init() override;

  /**
   * Starts sending a Payload without blocking. pollSend() drives it from
   * then on.
   *
   * @param[in] buf  A Payload, copied.
   * @param[in] len  Must be `sizeof(Payload)`.
   * @retval EXIT_SUCCESS  The send is in flight.
   * @retval EXIT_FAILURE  Wrong size, or a send is already in flight.
   */
  int beginSend(const uint8_t *buf, size_t len);

  /**
   * Moves the send in flight one step on.
   *
   * Each attempt sends the packet, switches to receive and listens up to
   * LoRaLinkConfig::ackTimeoutMs for a matching AckPayload, checking the radio
   * once per call. Packets of any other size heard while listening are
   * discarded. A failed attempt waits SEND_BACKOFF_MS before the next. Only
   * the call that transmits blocks, for the packet's airtime.
   *
   * @return LoRaSendStatus::Pending until the send ends, then Acked or Failed
   *         exactly once, then Idle until the next beginSend().
   */
  LoRaSendStatus pollSend();

  /**
   * Sends a Payload, blocking until its ACK arrives or every retry is used.
   *
   * Runs beginSend() and pollSend() to the end, so the attempts behave as
   * described there.
   *
   * @param[in] address  Ignored.
   * @param[in] buf      A Payload.
   * @param[in] len      Must be `sizeof(Payload)`.
   * @retval EXIT_SUCCESS  The matching ACK arrived.
   * @retval EXIT_FAILURE  Wrong size, a send already in flight, or every
   *                       attempt went unacknowledged.
   */
  int sendPacket(const void *address, const uint8_t *buf, size_t len) override;

  /**
   * Takes one received Payload and sends its ACK, without blocking.
   *
   * ACKs meant for other nodes and packets of the wrong size are discarded.
   *
   * @param[out] address  Ignored.
   * @param[out] buf      Destination for the Payload.
   * @param[in]  bufLen   Size of @p buf, at least `sizeof(Payload)`.
   * @return `sizeof(Payload)` on success, -1 if no usable Payload arrived, or
   *         -2 if @p buf is too small.
   */
  int receivePacket(void *address, uint8_t *buf, size_t bufLen) override;
};

#endif
