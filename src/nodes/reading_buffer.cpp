#include "reading_buffer.h"

#include <Arduino.h>

#include <cstring>

namespace {

constexpr size_t MAX_END_NODES = 8;
constexpr uint16_t READINGS_PER_NODE = 288;

struct Reading {
  uint32_t seq;
  float kwh_import;
  float kwh_export;
  float voltage;
  float battery_v;
};

struct NodeSlot {
  bool used;
  uint8_t uid[UID_LEN];
  uint8_t community_id;
  uint8_t unit_id;
  Reading ring[READINGS_PER_NODE];
  uint16_t head;
  uint16_t count;
};

NodeSlot slots[MAX_END_NODES];
size_t nextSlot = 0;
int peekedSlot = -1;

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
  r.battery_v = p.battery_v;
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
    out.battery_v = r.battery_v;
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
