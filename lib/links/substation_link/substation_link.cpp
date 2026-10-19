/**
 * @file
 * SubstationLink implementation.
 */

#include "substation_link.h"

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

/** printf format for a MAC address, used with MAC_ARGS(). */
#define MAC_FMT "%02X:%02X:%02X:%02X:%02X:%02X"
/** Expands a 6 byte MAC into the six arguments MAC_FMT expects. */
#define MAC_ARGS(m) (m)[0], (m)[1], (m)[2], (m)[3], (m)[4], (m)[5]

SubstationLink::SubstationLink(EspNowConfig config, const uint8_t *nodeUid)
    : uplink(config), joined(false), substationMac{}, epoch(0) {
  memcpy(uid, nodeUid, UID_LEN);
}

int SubstationLink::init() {
  return uplink.init();
}

bool SubstationLink::waitForBeacon(uint8_t *mac, uint32_t &beaconEpoch) {
  uint8_t buf[ESP_NOW_MAX_DATA_LEN];
  unsigned long start = millis();
  while (millis() - start < JOIN_BEACON_WAIT_MS) {
    int len = uplink.receivePacket(mac, buf, sizeof(buf));
    if (len < 0) {
      delay(10);
      continue;
    }
    LinkFrame frame;
    if (linkFrameParse(buf, len, frame) && frame.type == static_cast<uint8_t>(LinkFrameType::Beacon)) {
      beaconEpoch = frame.epoch;
      return true;
    }
  }
  return false;
}

bool SubstationLink::waitForJoinAck(const uint8_t *mac, uint32_t expectEpoch) {
  uint8_t buf[ESP_NOW_MAX_DATA_LEN];
  uint8_t from[ESP_NOW_ADDRESS_LEN];
  unsigned long start = millis();
  while (millis() - start < JOIN_ACK_WAIT_MS) {
    int len = uplink.receivePacket(from, buf, sizeof(buf));
    if (len < 0) {
      delay(5);
      continue;
    }
    LinkFrame frame;
    if (!linkFrameParse(buf, len, frame)
        || frame.type != static_cast<uint8_t>(LinkFrameType::JoinAck)
        || memcmp(from, mac, ESP_NOW_ADDRESS_LEN) != 0
        || frame.epoch != expectEpoch
        || !uidEquals(frame.uid, uid)) {
      continue;
    }
    return true;
  }
  return false;
}

int SubstationLink::join() {
  if (joined) {
    return EXIT_SUCCESS;
  }

  uint8_t mac[ESP_NOW_ADDRESS_LEN];
  uint32_t beaconEpoch = 0;
  if (!waitForBeacon(mac, beaconEpoch)) {
    Serial.println("[Link] No substation beacon heard");
    return EXIT_FAILURE;
  }
  Serial.printf("[Link] Beacon from " MAC_FMT ", joining\n", MAC_ARGS(mac));

  if (uplink.addPeer(EspNowPeerConfig{{mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]}}) != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }

  LinkFrame request = linkFrameMake(LinkFrameType::Join, beaconEpoch, uid);
  for (uint8_t attempt = 0; attempt < JOIN_ATTEMPTS; attempt++) {
    if (uplink.sendPacket(mac, reinterpret_cast<const uint8_t *>(&request), sizeof(request)) != EXIT_SUCCESS) {
      continue;
    }
    if (waitForJoinAck(mac, beaconEpoch)) {
      memcpy(substationMac, mac, ESP_NOW_ADDRESS_LEN);
      epoch = beaconEpoch;
      joined = true;
      Serial.printf("[Link] Joined substation " MAC_FMT "\n", MAC_ARGS(mac));
      return EXIT_SUCCESS;
    }
  }

  Serial.printf("[Link] No JoinAck from " MAC_FMT "\n", MAC_ARGS(mac));
  uplink.removePeer(mac);
  return EXIT_FAILURE;
}

void SubstationLink::drainReceived() {
  uint8_t buf[ESP_NOW_MAX_DATA_LEN];
  uint8_t from[ESP_NOW_ADDRESS_LEN];
  int len;
  while ((len = uplink.receivePacket(from, buf, sizeof(buf))) != -1) {
    LinkFrame frame;
    if (!joined || len < 0 || !linkFrameParse(buf, len, frame)) {
      continue;
    }
    // Our substation rebooted, so it has forgotten we joined
    if (frame.type == static_cast<uint8_t>(LinkFrameType::Beacon)
        && memcmp(from, substationMac, ESP_NOW_ADDRESS_LEN) == 0
        && frame.epoch != epoch) {
      Serial.println("[Link] Substation rebooted, rejoining");
      forget();
    }
  }
}

void SubstationLink::forget() {
  uplink.removePeer(substationMac);
  joined = false;
}

int SubstationLink::send(const Payload &p) {
  drainReceived();

  // The radio already retried the frame, so one failure means the substation
  // may be gone. Forget it, rejoin (possibly another substation) and resend
  for (uint8_t attempt = 0; attempt < SEND_ATTEMPTS; attempt++) {
    // Returns at once while joined
    if (join() != EXIT_SUCCESS) {
      return EXIT_FAILURE;
    }
    if (uplink.sendPacket(substationMac, reinterpret_cast<const uint8_t *>(&p), sizeof(p)) == EXIT_SUCCESS) {
      return EXIT_SUCCESS;
    }
    Serial.println("[Link] Send failed, rejoining");
    forget();
  }
  return EXIT_FAILURE;
}
