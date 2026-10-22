/**
 * @file
 * SubstationLink, a meter node's connection to whichever substation is in range.
 */

#ifndef SUBSTATION_LINK_H
#define SUBSTATION_LINK_H

#include <cstdint>

#include "esp_now_uplink.h"
#include "networking_config.h"
#include "shared_payload.h"

/** Longest a join waits to hear any substation's beacon, in ms. */
constexpr uint32_t JOIN_BEACON_WAIT_MS = 3000;
/**
 * Longest each Join waits for its JoinAck, in ms.
 *
 * Long enough to outlast one LoRa relay on the substation (3 tries of about
 * 1.8 s with the default lora_retries and lora_ack_ms), during which it
 * cannot answer. A Join the radio confirmed waits in the substation's queue
 * and is answered once the relay ends.
 */
constexpr uint32_t JOIN_ACK_WAIT_MS = 2500;
/**
 * Longest send() waits for the substation's AckPayload for a reading, in ms.
 *
 * The substation answers from its loop(), so this has to outlast one pass of
 * it. The slowest pass holds one LoRa transmit (about 0.4 s for a Payload at
 * SF10 and 125 kHz) plus a few ESP-NOW sends.
 */
constexpr uint32_t DELIVERY_ACK_WAIT_MS = 1000;
/** Join frames sent before a join attempt gives up. */
constexpr uint8_t JOIN_ATTEMPTS = 3;
/** Tries a reading gets per send(), with a rejoin between them. */
constexpr uint8_t SEND_ATTEMPTS = 2;

/**
 * Finds a substation over ESP-NOW, joins it, and sends it readings.
 *
 * Owns the node's EspNowUplink and hides the discovery handshake from the
 * node. The substation broadcasts LinkFrameType::Beacon frames; join() takes
 * the first one it hears, then trades a Join for a JoinAck with that
 * substation. Only then does send() deliver readings, which a substation drops
 * from any node that has not joined. A reading only counts as delivered once
 * the substation answers it with an AckPayload, which it sends after the
 * reading is in its buffer.
 *
 * A joined substation is forgotten, and rejoined on the next send(), when its
 * beacon shows a new epoch (it rebooted and lost its joined table). It is also
 * forgotten as soon as a reading fails to send or goes unacknowledged (it may
 * be gone, or may have forgotten us), and send() then rejoins straight away,
 * so a node without a substation retries discovery on every reading.
 */
class SubstationLink {
 private:
  EspNowUplink uplink;                         ///< Radio link the handshake and readings travel over.
  uint8_t uid[UID_LEN];                        ///< This node's UID, sent in every Join.
  bool joined;                                 ///< True while a substation has acknowledged our Join.
  uint8_t substationMac[ESP_NOW_ADDRESS_LEN];  ///< Joined substation's MAC, valid while #joined.
  uint32_t epoch;                              ///< Joined substation's boot epoch, valid while #joined.

  /**
   * Waits for any substation's beacon.
   *
   * @param[out] mac          Beacon sender's MAC.
   * @param[out] beaconEpoch  Epoch the beacon carried.
   * @retval true   A beacon arrived within JOIN_BEACON_WAIT_MS.
   * @retval false  None did.
   */
  bool waitForBeacon(uint8_t *mac, uint32_t &beaconEpoch);

  /**
   * Waits for the JoinAck that answers our Join.
   *
   * Frames from other senders, other types, or carrying another UID or epoch
   * are skipped.
   *
   * @param[in] mac          Substation the Join went to.
   * @param[in] expectEpoch  Epoch the Join carried.
   * @retval true   The matching JoinAck arrived within JOIN_ACK_WAIT_MS.
   * @retval false  It did not.
   */
  bool waitForJoinAck(const uint8_t *mac, uint32_t expectEpoch);

  /**
   * Waits for the joined substation's AckPayload for a reading.
   *
   * Frames from other senders, of another size, or carrying another UID or
   * sequence number are skipped.
   *
   * @param[in] seq  Sequence number of the reading sent.
   * @retval true   The matching ACK arrived within DELIVERY_ACK_WAIT_MS.
   * @retval false  It did not.
   */
  bool waitForDeliveryAck(uint32_t seq);

  /**
   * Empties the receive queue, forgetting the substation if its beacon shows
   * it rebooted.
   */
  void drainReceived();

  /** Unregisters the joined substation, so the next send() joins again. */
  void forget();

 public:
  /**
   * Stores the config. The radio is not touched until init().
   *
   * @param[in] config   Channel and send timeout. The channel must match the substation's.
   * @param[in] nodeUid  This node's UID_LEN byte UID.
   */
  SubstationLink(EspNowConfig config, const uint8_t *nodeUid);

  /**
   * Brings up ESP-NOW.
   *
   * @retval EXIT_SUCCESS  Ready to join.
   * @retval EXIT_FAILURE  The uplink failed to start.
   */
  int init();

  /**
   * Discovers a substation and joins it, blocking for at most
   * JOIN_BEACON_WAIT_MS plus JOIN_ATTEMPTS times JOIN_ACK_WAIT_MS.
   *
   * @retval EXIT_SUCCESS  Joined, or already joined.
   * @retval EXIT_FAILURE  No beacon heard, or no JoinAck after every attempt.
   */
  int join();

  /**
   * Sends one reading to the joined substation, joining first if needed.
   *
   * A try succeeds once the substation acknowledges buffering the reading.
   * If the send fails or no ACK arrives within DELIVERY_ACK_WAIT_MS, the
   * substation is forgotten, a substation is joined again (possibly a
   * different one) and the reading is resent, up to SEND_ATTEMPTS tries in
   * all.
   *
   * @param[in] p  Reading to send.
   * @retval EXIT_SUCCESS  The substation acknowledged buffering the reading.
   * @retval EXIT_FAILURE  No substation could be joined, or every try failed
   *                       or went unacknowledged.
   */
  int send(const Payload &p);
};

#endif
