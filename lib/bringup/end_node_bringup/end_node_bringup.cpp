/**
 * @file
 * endNodeBringUp() and MeterGate implementations.
 */

#include "end_node_bringup.h"

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "seq_counter.h"
#include "wifi_radio.h"

int endNodeBringUp(const EndNodeParts &parts, SubstationLink *&link, Payload &payload) {
  if (seqCounterBegin() != EXIT_SUCCESS) {
    Serial.printf("[%s] NVS unavailable for the sequence counter, idling\n", parts.tag);
    return EXIT_FAILURE;
  }

  if (parts.ap != nullptr && !wifiRadioStartAp(*parts.ap, parts.espNow.channel)) {
    Serial.printf("[%s] SoftAP bring-up failed, idling\n", parts.tag);
    return EXIT_FAILURE;
  }

  if (parts.bus->init() != EXIT_SUCCESS) {
    if (parts.busPolicy == BusPolicy::Required) {
      Serial.printf("[%s] Bus init failed, idling\n", parts.tag);
      return EXIT_FAILURE;
    }
    Serial.printf("[%s] Bus not reachable yet, the meter check will retry\n", parts.tag);
  }

  if (parts.reader->init(*parts.bus) != EXIT_SUCCESS) {
    Serial.printf("[%s] Reader init failed, idling\n", parts.tag);
    return EXIT_FAILURE;
  }

  // ESP-NOW runs on the station interface, beside the AP when there is one
  link = new SubstationLink(parts.espNow, parts.uid);
  if (link->init() != EXIT_SUCCESS) {
    Serial.printf("[%s] ESP-NOW init failed, idling\n", parts.tag);
    return EXIT_FAILURE;
  }

  // Not fatal: send() joins on the first reading if no substation answers now
  link->join();

  memcpy(payload.uid, parts.uid, sizeof(payload.uid));
  payload.community_id = parts.communityId;
  payload.unit_id = parts.unitId;
  payload.node_type = static_cast<uint8_t>(parts.nodeType);
  return EXIT_SUCCESS;
}

MeterGate::MeterGate(const char *tag, Reader &reader, uint32_t retryMs, uint8_t lostAfter)
    : tag(tag), reader(reader), retryMs(retryMs), lostAfter(lostAfter) {}

bool MeterGate::ready() {
  if (isReady) {
    return true;
  }
  if (!probeDue && millis() - lastProbeMs < retryMs) {
    return false;
  }
  probeDue = false;
  lastProbeMs = millis();

  if (reader.probe() != EXIT_SUCCESS) {
    Serial.printf("[%s] Meter check failed, retrying in %lu s\n", tag,
                  static_cast<unsigned long>((retryMs + 999) / 1000));
    return false;
  }
  Serial.printf("[%s] Ready\n", tag);
  isReady = true;
  failedReads = 0;
  return true;
}

void MeterGate::reportRead(int status) {
  if (!isReady) {
    return;
  }
  if (status == EXIT_SUCCESS) {
    failedReads = 0;
    return;
  }
  if (++failedReads < lostAfter) {
    return;
  }
  Serial.printf("[%s] Meter lost after %u failed reads, re-checking\n", tag, failedReads);
  isReady = false;
  probeDue = true;
}
