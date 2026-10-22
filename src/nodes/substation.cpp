/**
 * @file
 * Substation node: relays meter readings from ESP-NOW to LoRa.
 *
 * Meter nodes find the substation through a beacon it broadcasts every
 * BEACON_INTERVAL_MS, then join with a Join it answers with a JoinAck (see
 * LinkFrame). Only readings from joined nodes are accepted. Each beacon
 * carries a random epoch picked at boot, so a joined node notices a reboot
 * (which empties the joined table) and joins again.
 *
 * Readings arriving over ESP-NOW go into the reading buffer straight away.
 * One buffered reading at a time is then relayed over LoRa, whose ACK and
 * retry scheme lives in LoRaLink. The relay runs a step per loop() pass
 * rather than blocking, so beacons, Joins and new readings are still served
 * while it waits on the gateway. A reading only leaves the buffer once the
 * gateway acknowledges it. After a failed relay the substation waits
 * RETRY_BACKOFF_MS before trying again, while still buffering new readings.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_random.h>

#include <cstdlib>
#include <cstring>

#include "esp_now_uplink.h"
#include "loramodule.h"
#include "lora_link.h"
#include "nodes.h"
#include "substation_config.h"
#include "nvs_config.h"
#include "reading_buffer.h"
#include "shared_payload.h"

static LoRaModule *radio = nullptr;     ///< LoRa radio.
static LoRaLink *loraLink = nullptr;    ///< ACK layer on top of #radio, toward the gateway.
static EspNowUplink *uplink = nullptr;  ///< ESP-NOW link to the meter nodes.

static SubstationConfig cfg;  ///< Config loaded at setup.

/** Wait after a failed relay before trying again, in ms. */
static constexpr unsigned long RETRY_BACKOFF_MS = 30UL * 1000UL;

/** Time between discovery beacons, in ms. */
static constexpr unsigned long BEACON_INTERVAL_MS = 1000;

/** A meter node that completed the discovery handshake. */
struct JoinedNode {
  uint8_t mac[ESP_NOW_ADDRESS_LEN];  ///< Node's station MAC, the address readings arrive from.
  uint8_t uid[UID_LEN];              ///< UID the node joined with.
};

/** Joined nodes, capped at the reading buffer's node slots so every joined node can be buffered. */
static JoinedNode joined[READING_BUFFER_MAX_NODES];
static size_t joinedCount = 0;  ///< Valid entries at the start of #joined.

static uint32_t epoch = 0;              ///< Random per boot epoch, sent in every beacon.
static unsigned long nextBeaconAt = 0;  ///< millis() at which the next beacon is due.
static unsigned long nextSendAt = 0;    ///< millis() before which no relay is attempted.
static Payload inFlight;                ///< Reading being relayed, valid while #loraLink has a send pending.
static bool ready = false;              ///< True once setup fully succeeded.

/** printf format for a MAC address, used with MAC_ARGS(). */
#define MAC_FMT "%02X:%02X:%02X:%02X:%02X:%02X"
/** Expands a 6 byte MAC into the six arguments MAC_FMT expects. */
#define MAC_ARGS(m) (m)[0], (m)[1], (m)[2], (m)[3], (m)[4], (m)[5]

void substationSetup() {
  if (loadSubstationConfig(cfg) != EXIT_SUCCESS) {
    Serial.println("[Substation] NVS unavailable, idling");
    return;
  }

  radio = new LoRaModule(cfg.lora);
  loraLink = new LoRaLink(*radio, cfg.link);

  if(radio->init() != EXIT_SUCCESS){
    Serial.println("[Substation] LoRa module init, failed idling");
    return;
  }

  if (loraLink->init() != EXIT_SUCCESS) {
    Serial.println("[Substation] LoRa init failed, idling");
    return;
  }

  uplink = new EspNowUplink(EspNowConfig{cfg.espNowChannel, cfg.espNowTimeoutMs});
  if (uplink->init() != EXIT_SUCCESS) {
    Serial.println("[Substation] ESP-NOW init failed, idling");
    return;
  }

  EspNowPeerConfig broadcast{};
  memcpy(broadcast.mac, ESP_NOW_BROADCAST_MAC, ESP_NOW_ADDRESS_LEN);
  if (uplink->addPeer(broadcast) != EXIT_SUCCESS) {
    Serial.println("[Substation] Broadcast peer failed, idling");
    return;
  }

  epoch = esp_random();

  Serial.println(">>> MAC Address: " + WiFi.macAddress() + " <<<");
  Serial.printf("SUBSTATION: Beaconing on channel %u, epoch %08lX\n", cfg.espNowChannel, static_cast<unsigned long>(epoch));

  ready = true;
}

/**
 * Looks up a node in the joined table.
 *
 * @param[in] mac  Node's MAC.
 * @return Index into #joined, or -1 if the node has not joined.
 */
static int findJoined(const uint8_t *mac) {
  for (size_t i = 0; i < joinedCount; i++) {
    if (memcmp(joined[i].mac, mac, ESP_NOW_ADDRESS_LEN) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

/** Broadcasts a discovery beacon once BEACON_INTERVAL_MS has passed since the last. */
static void beaconIfDue() {
  // Signed difference so the check still works when millis() wraps
  if (static_cast<long>(millis() - nextBeaconAt) < 0) {
    return;
  }
  nextBeaconAt = millis() + BEACON_INTERVAL_MS;

  LinkFrame beacon = linkFrameMake(LinkFrameType::Beacon, epoch, nullptr);
  uplink->sendPacket(ESP_NOW_BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&beacon), sizeof(beacon));
}

/**
 * Answers a node's Join: adds it to the joined table, or refreshes it if it
 * rejoined after a reboot, then sends a JoinAck.
 *
 * Joins carrying another epoch answered an old beacon and are ignored, as are
 * new nodes once the table is full. Either way the node gets no JoinAck and
 * keeps looking.
 *
 * @param[in] mac    Node's MAC.
 * @param[in] frame  The parsed Join.
 */
static void handleJoin(const uint8_t *mac, const LinkFrame &frame) {
  if (frame.epoch != epoch) {
    return;
  }

  char uidHex[UID_HEX_LEN];
  int index = findJoined(mac);
  if (index < 0) {
    if (joinedCount == READING_BUFFER_MAX_NODES) {
      Serial.printf("[Substation] Join refused, table full | UID: %s | MAC: " MAC_FMT "\n", uidToHex(frame.uid, uidHex), MAC_ARGS(mac));
      return;
    }
    index = static_cast<int>(joinedCount++);
    memcpy(joined[index].mac, mac, ESP_NOW_ADDRESS_LEN);
  }
  memcpy(joined[index].uid, frame.uid, UID_LEN);

  EspNowPeerConfig peer{};
  memcpy(peer.mac, mac, ESP_NOW_ADDRESS_LEN);
  if (uplink->addPeer(peer) != EXIT_SUCCESS) {
    return;
  }

  // A lost JoinAck is fine, the node sends its Join again and gets another
  LinkFrame ack = linkFrameMake(LinkFrameType::JoinAck, epoch, frame.uid);
  uplink->sendPacket(mac, reinterpret_cast<const uint8_t *>(&ack), sizeof(ack));

  Serial.printf("[Substation] Joined UID: %s | MAC: " MAC_FMT " | Nodes: %u/%u\n", uidToHex(frame.uid, uidHex), MAC_ARGS(mac), joinedCount, READING_BUFFER_MAX_NODES);
}

/**
 * Handles every frame waiting in the ESP-NOW queue: readings from joined
 * nodes go into the reading buffer, Joins are answered.
 */
static void handleIncoming() {
  uint8_t mac[ESP_NOW_ADDRESS_LEN];
  uint8_t buf[ESP_NOW_MAX_DATA_LEN];
  int len;
  while ((len = uplink->receivePacket(mac, buf, sizeof(buf))) != -1) {
    LinkFrame frame;
    if (linkFrameParse(buf, len, frame)) {
      if (frame.type == static_cast<uint8_t>(LinkFrameType::Join)) {
        handleJoin(mac, frame);
      }
      continue;
    }

    if (len != static_cast<int>(sizeof(Payload))) {
      Serial.println("[Error] Payload size mismatch!");
      continue;
    }

    if (findJoined(mac) < 0) {
      Serial.printf("[Substation] Dropped reading from unjoined MAC: " MAC_FMT "\n", MAC_ARGS(mac));
      continue;
    }

    Payload incoming;
    memcpy(&incoming, buf, sizeof(incoming));
    char uidHex[UID_HEX_LEN];
    readingBufferPush(incoming);
    Serial.printf("[Substation] Buffered UID: %s | SEQ: %u | Total: %u\n", uidToHex(incoming.uid, uidHex), incoming.seq, readingBufferCount());
  }
}

/**
 * Moves the LoRa relay one step on without blocking, so beacons and node
 * frames keep being served while a reading is in flight.
 *
 * While a relay is in flight, polls it: an ACKed reading leaves the buffer,
 * a failed one stays and holds off the next relay for RETRY_BACKOFF_MS.
 * Otherwise, once that backoff is over, starts relaying the next buffered
 * reading.
 */
static void relayStep() {
  char uidHex[UID_HEX_LEN];

  switch (loraLink->pollSend()) {
    case LoRaSendStatus::Pending:
      return;

    case LoRaSendStatus::Acked:
      readingBufferPop();
      Serial.printf("[Substation] Relay SUCCESS | UID: %s | SEQ: %u | Remaining: %u\n", uidToHex(inFlight.uid, uidHex), inFlight.seq, readingBufferCount());
      return;

    case LoRaSendStatus::Failed:
      nextSendAt = millis() + RETRY_BACKOFF_MS;
      Serial.printf("[Substation] Relay FAILED | UID: %s | SEQ: %u | Kept, retry in %lus | Buffered: %u\n", uidToHex(inFlight.uid, uidHex), inFlight.seq, RETRY_BACKOFF_MS / 1000, readingBufferCount());
      return;

    case LoRaSendStatus::Idle:
      break;
  }

  // Signed difference so the check still works when millis() wraps
  if (static_cast<long>(millis() - nextSendAt) < 0) {
    return;
  }

  if (!readingBufferPeek(inFlight)) {
    return;
  }

  Serial.printf("[Substation] Relaying UID: %s | SEQ: %u\n", uidToHex(inFlight.uid, uidHex), inFlight.seq);
  loraLink->beginSend(reinterpret_cast<const uint8_t *>(&inFlight), sizeof(Payload));
}

void substationLoop() {
  if (!ready) {
    return;
  }

  beaconIfDue();
  handleIncoming();
  relayStep();
}
