/**
 * @file
 * Payload JSON implementation.
 */

#include "payload_json.h"

#include <ArduinoJson.h>

namespace {

/**
 * Names a node type for the JSON.
 *
 * @param[in] type  Raw Payload::node_type.
 * @return "rs485", "cv" or "ir", or nullptr for a value no NodeType has.
 */
const char *nodeTypeName(uint8_t type) {
  switch (static_cast<NodeType>(type)) {
  case NodeType::Rs485:
    return "rs485";
  case NodeType::Cv:
    return "cv";
  case NodeType::Ir:
    return "ir";
  }
  return nullptr;
}

}  // namespace

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
  const char *type = nodeTypeName(p.node_type);
  if (type != nullptr) {
    doc["node_type"] = type;
  } else {
    doc["node_type"] = nullptr;
  }
  doc["rssi"] = rssi;
  doc["snr"] = snr;
  return serializeJson(doc, out, outLen);
}