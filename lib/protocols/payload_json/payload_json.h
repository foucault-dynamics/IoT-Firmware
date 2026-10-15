/**
 * @file
 * The JSON the gateway publishes on MQTT for each reading.
 */

#ifndef PAYLOAD_JSON_H
#define PAYLOAD_JSON_H

#include <cstddef>
#include <ctime>

#include "shared_payload.h"

static constexpr time_t MIN_VALID_EPOCH = 1700000000;  ///< Clock earlier than this (Nov 2023) means NTP has not synced yet.
static constexpr size_t TIMESTAMP_LEN = 32;            ///< Buffer size for an ISO 8601 timestamp.

/**
 * Formats a time as ISO 8601 local time, e.g. "2026-10-07T14:03:00+10:00".
 *
 * @param[in]  now     Time to format.
 * @param[out] out     Destination buffer.
 * @param[in]  outLen  Size of @p out in bytes.
 * @retval true   @p out holds the timestamp.
 * @retval false  NTP has not synced yet, or @p out is too small.
 */
bool formatTimestamp(time_t now, char *out, size_t outLen);

/**
 * Serialises a reading and its signal quality to the JSON published on MQTT.
 *
 * "ts" is null when NTP has not synced yet.
 *
 * @param[in]  p       Reading to serialise.
 * @param[in]  rssi    Packet RSSI in dBm.
 * @param[in]  snr     Packet SNR in dB.
 * @param[in]  now     Time of publishing.
 * @param[out] out     Destination buffer.
 * @param[in]  outLen  Size of @p out in bytes.
 * @return Number of bytes written, not counting the terminator.
 */
size_t buildPayloadJson(const Payload &p, int rssi, float snr, time_t now, char *out, size_t outLen);

#endif