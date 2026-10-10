/**
 * @file
 * Payload JSON implementation.
 */

#include "payload_json.h"

#include <ArduinoJson.h>

bool formatTimestamp(time_t now, char *out, size_t outLen) {
  if (now < MIN_VALID_EPOCH) {
    return false;
  }
  struct tm local;
  localtime_r(&now, &local);
  size_t len = strftime(out, outLen, "%Y-%m-%dT%H:%M:%S%z", &local);
  if (len < 2 || len + 2 > outLen) {
    return false;
  }
  // strftime gives "+1000", ISO 8601 wants "+10:00", so insert the colon
  out[len + 1] = '\0';
  out[len] = out[len - 1];
  out[len - 1] = out[len - 2];
  out[len - 2] = ':';
  return true;
}

size_t buildPayloadJson(const Payload &p, int rssi, float snr, time_t now, char *out, size_t outLen) {
  char uidHex[UID_HEX_LEN];
  JsonDocument doc;
  char ts[TIMESTAMP_LEN];
  if (formatTimestamp(now, ts, sizeof(ts))) {
    doc["ts"] = ts;
  } else {
    doc["ts"] = nullptr;
  }
  doc["uid"] = uidToHex(p.uid, uidHex);
  doc["seq"] = p.seq;
  doc["kwh_import"] = p.kwh_import;
  doc["kwh_export"] = p.kwh_export;
  doc["voltage"] = p.voltage;
  doc["community_id"] = p.community_id;
  doc["unit_id"] = p.unit_id;
  doc["rssi"] = rssi;
  doc["snr"] = snr;
  return serializeJson(doc, out, outLen);
}