/**
 * @file
 * The bring up every end node shares: one ordered set of peripheral checks,
 * and the MeterGate that keeps a node not ready until its meter answers.
 */

#ifndef END_NODE_BRINGUP_H
#define END_NODE_BRINGUP_H

#include <cstdint>

#include "module.h"
#include "networking_config.h"
#include "reader.h"
#include "shared_payload.h"
#include "substation_link.h"

/** How a failed bus init() is treated during bring up. */
enum class BusPolicy : uint8_t {
  Required,  ///< A failed init() is fatal. Every real bus.
  Tolerated  ///< A failed init() is logged and bring up carries on. TCP
             ///< simulator buses, whose host may not have joined the AP yet.
};

/** Everything endNodeBringUp() needs from a node, built by the node first. */
struct EndNodeParts {
  const char *tag;          ///< Log prefix, e.g. "RS485".
  NodeType nodeType;        ///< Copied into the payload.
  const uint8_t *uid;       ///< UID_LEN bytes, this board's UID.
  uint8_t communityId;      ///< Copied into the payload.
  uint8_t unitId;           ///< Copied into the payload.
  EspNowConfig espNow;      ///< Uplink channel and timeout.
  const SoftApConfig *ap;   ///< AP to host, or nullptr for no AP.
  Module *bus;              ///< Bus the meter is on, not yet initialised.
  Reader *reader;           ///< Reader for #bus, not yet initialised.
  BusPolicy busPolicy;      ///< Whether a failed bus init() is fatal.
};

/**
 * Brings up an end node's peripherals in one fixed order, logging and stopping
 * at the first fatal failure.
 *
 * 1. The sequence counter's NVS.
 * 2. The AP, if EndNodeParts::ap is set.
 * 3. The bus's init(). Fatal only for BusPolicy::Required.
 * 4. The reader's init().
 * 5. ESP-NOW, by creating and initialising @p link.
 * 6. A first join to a substation. Not fatal, send() joins later.
 * 7. The payload's identity fields.
 *
 * This proves the board's own peripherals. Whether a meter answers is
 * MeterGate's job, since a meter can be absent at boot and arrive later.
 * Nothing is freed on failure, because the node then idles until reboot.
 *
 * @param[in]  parts    The node's config, bus and reader.
 * @param[out] link     The substation link. Set once step 5 starts.
 * @param[out] payload  Its uid, community_id, unit_id and node_type are set.
 * @retval EXIT_SUCCESS  Every step passed. The node may start its MeterGate.
 * @retval EXIT_FAILURE  A fatal step failed. The reason is logged.
 */
int endNodeBringUp(const EndNodeParts &parts, SubstationLink *&link, Payload &payload);

/** Time between meter probes while the meter is not answering, in ms. */
constexpr uint32_t PROBE_RETRY_MS = 5000;
/** Consecutive failed reads before a ready node checks its meter again. */
constexpr uint8_t READS_BEFORE_LOST = 3;

/**
 * Keeps a node not ready until its meter answers a probe, and drops it back to
 * not ready when reads keep failing.
 *
 * The node calls ready() at the top of every loop and only reads once it
 * returns true, then passes every read's status to reportRead(). Once ready,
 * ready() does no I/O at all.
 */
class MeterGate {
 public:
  /**
   * Stores the reader. Nothing is probed until the first ready().
   *
   * @param[in] tag        Log prefix, e.g. "RS485".
   * @param[in] reader     Reader to probe. Must outlive the gate.
   * @param[in] retryMs    Time between probes while not ready, in ms.
   * @param[in] lostAfter  Consecutive failed reads that make the node not ready.
   */
  MeterGate(const char *tag, Reader &reader, uint32_t retryMs = PROBE_RETRY_MS,
            uint8_t lostAfter = READS_BEFORE_LOST);

  /**
   * Says whether the node may read its meter, probing it when one is due.
   *
   * Not ready, a probe runs on the first call and after the meter is lost,
   * and otherwise at most once per retry interval. A passing probe logs
   * "[TAG] Ready".
   *
   * @retval true   The meter has answered. Read it.
   * @retval false  The meter has not answered yet. Try again next loop.
   */
  bool ready();

  /**
   * Records the result of a read. Ignored while not ready.
   *
   * A success clears the failure count. The lostAfter-th failure in a row
   * logs it and makes the node not ready, and the next ready() probes
   * straight away.
   *
   * @param[in] status  EXIT_SUCCESS or EXIT_FAILURE from the read.
   */
  void reportRead(int status);

 private:
  const char *tag;          ///< Log prefix.
  Reader &reader;           ///< Reader to probe.
  uint32_t retryMs;         ///< Time between probes while not ready, in ms.
  uint8_t lostAfter;        ///< Failed reads in a row that lose the meter.
  bool isReady = false;     ///< True once a probe passed, until the meter is lost.
  bool probeDue = true;     ///< True when the next ready() probes regardless of time.
  uint32_t lastProbeMs = 0; ///< millis() of the last probe.
  uint8_t failedReads = 0;  ///< Failed reads in a row while ready.
};

#endif
