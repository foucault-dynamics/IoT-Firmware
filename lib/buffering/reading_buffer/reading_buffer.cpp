/**
 * @file
 * Reading buffer implementation: fixed node slots, each with its own ring.
 */

#include "reading_buffer.h"

#include <Arduino.h>

#include <cstring>

namespace {

/** Most meter nodes one substation can buffer for. */
constexpr size_t MAX_END_NODES = 8;
/** Readings held per node. 288 is one day at one reading every 5 minutes. */
constexpr uint16_t READINGS_PER_NODE = 288;

/** One buffered reading, without the per node fields NodeSlot already holds. */
struct Reading {
  uint32_t seq;      ///< Payload::seq.
  float kwh_import;  ///< Payload::kwh_import.
  float kwh_export;  ///< Payload::kwh_export.
  float voltage;     ///< Payload::voltage.
};

/** Everything buffered for one meter node. */
struct NodeSlot {
  bool used;                        ///< False while the slot is free.
  uint8_t uid[UID_LEN];             ///< Node this slot belongs to.
  uint8_t community_id;             ///< Latest community ID the node sent.
  uint8_t unit_id;                  ///< Latest unit ID the node sent.
  Reading ring[READINGS_PER_NODE];  ///< Readings, oldest at #head.
  uint16_t head;                    ///< Index of the oldest reading in #ring.
  uint16_t count;                   ///< Number of readings held.
};

NodeSlot slots[MAX_END_NODES];  ///< Every node slot.
size_t nextSlot = 0;            ///< Slot the next peek starts searching from.
int peekedSlot = -1;            ///< Slot of the last peeked reading, or -1.

/**
 * Finds the slot for a UID, claiming a free one if it is new.
 *
 * @param[in] uid  Node's UID.
 * @return The node's slot, or nullptr if the UID is new and none are free.
 */
NodeSlot *findOrClaimSlot(const uint8_t *uid) {
  NodeSlot *freeSlot = nullptr;
  for (NodeSlot &slot : slots) {
    if (slot.used && uidEquals(slot.uid, uid)) {
      return &slot;
    }
    if (!slot.used && freeSlot == nullptr) {
      freeSlot = &slot;
    }
  }
  if (freeSlot != nullptr) {
    memset(freeSlot, 0, sizeof(NodeSlot));
    freeSlot->used = true;
    memcpy(freeSlot->uid, uid, UID_LEN);
  }
  return freeSlot;
}

}  // namespace

bool readingBufferPush(const Payload &p) {
  char uidHex[UID_HEX_LEN];
  NodeSlot *slot = findOrClaimSlot(p.uid);
  if (slot == nullptr) {
    Serial.printf("[Buffer] No free slot for UID: %s | SEQ: %u dropped\n", uidToHex(p.uid, uidHex), p.seq);
    return false;
  }

  slot->community_id = p.community_id;
  slot->unit_id = p.unit_id;

  // A full ring drops its oldest reading to make room
  if (slot->count == READINGS_PER_NODE) {
    Serial.printf("[Buffer] Full for UID: %s | oldest SEQ: %u overwritten\n", uidToHex(p.uid, uidHex), slot->ring[slot->head].seq);
    slot->head = (slot->head + 1) % READINGS_PER_NODE;
    slot->count--;
  }

  Reading &r = slot->ring[(slot->head + slot->count) % READINGS_PER_NODE];
  r.seq = p.seq;
  r.kwh_import = p.kwh_import;
  r.kwh_export = p.kwh_export;
  r.voltage = p.voltage;
  slot->count++;
  return true;
}

bool readingBufferPeek(Payload &out) {
  for (size_t i = 0; i < MAX_END_NODES; i++) {
    size_t index = (nextSlot + i) % MAX_END_NODES;
    const NodeSlot &slot = slots[index];
    if (!slot.used || slot.count == 0) {
      continue;
    }

    const Reading &r = slot.ring[slot.head];
    memcpy(out.uid, slot.uid, UID_LEN);
    out.seq = r.seq;
    out.kwh_import = r.kwh_import;
    out.kwh_export = r.kwh_export;
    out.voltage = r.voltage;
    out.community_id = slot.community_id;
    out.unit_id = slot.unit_id;
    peekedSlot = static_cast<int>(index);
    return true;
  }
  peekedSlot = -1;
  return false;
}

void readingBufferPop() {
  if (peekedSlot < 0) {
    return;
  }
  NodeSlot &slot = slots[peekedSlot];
  if (slot.count > 0) {
    slot.head = (slot.head + 1) % READINGS_PER_NODE;
    slot.count--;
  }
  nextSlot = (peekedSlot + 1) % MAX_END_NODES;
  peekedSlot = -1;
}

size_t readingBufferCount() {
  size_t total = 0;
  for (const NodeSlot &slot : slots) {
    total += slot.count;
  }
  return total;
}
