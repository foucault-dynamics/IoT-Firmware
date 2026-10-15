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
/** Join frames sent before a join attempt gives up. */
constexpr uint8_t JOIN_ATTEMPTS = 3;
/** Failed reading sends in a row before the substation is forgotten and rejoined. */
constexpr uint8_t MAX_SEND_FAILURES = 3;

/**
 * Finds a substation over ESP-NOW, joins it, and sends it readings.
 *
 * Owns the node's EspNowUplink and hides the discovery handshake from the
 * node. The substation broadcasts LinkFrameType::Beacon frames; join() takes
 * the first one it hears, then trades a Join for a JoinAck with that
 * substation. Only then does send() deliver readings, which a substation drops
 * from any node that has not joined.
 *
 * A joined substation is forgotten, and rejoined on the next send(), when its
 * beacon shows a new epoch (it rebooted and lost its joined table) or after
 * MAX_SEND_FAILURES failed sends in a row (it is gone).
 */
class SubstationLink {
 private:
  EspNowUplink uplink;                         ///< Radio link the handshake and readings travel over.
  uint8_t uid[UID_LEN];                        ///< This node's UID, sent in every Join.
  bool joined;                                 ///< True while a substation has acknowledged our Join.
  uint8_t substationMac[ESP_NOW_ADDRESS_LEN];  ///< Joined substation's MAC, valid while #joined.
  uint32_t epoch;                              ///< Joined substation's boot epoch, valid while #joined.
  uint8_t sendFailures;                        ///< Failed send() calls in a row since the last success.

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
   * @param[in] p  Reading to send.
   * @retval EXIT_SUCCESS  The substation's radio acknowledged the reading.
   * @retval EXIT_FAILURE  No substation could be joined, or the send failed.
   */
  int send(const Payload &p);
};

#endif
