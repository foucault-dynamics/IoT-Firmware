/**
 * @file
 * NVS read helpers and the LoRa loaders, shared by the per node loaders.
 *
 * Internal to nvs_config. Each helper reads one key, falls back to a default
 * when the key was never set, and logs the value when it came from NVS. The
 * caller must have opened #prefs on #NVS_NAMESPACE first.
 */

#ifndef NVS_READ_H
#define NVS_READ_H

#include <Preferences.h>

#include <cstddef>
#include <cstdint>

#include "networking_config.h"

/** NVS namespace every config key lives in. */
extern const char *NVS_NAMESPACE;
/** Shared NVS handle. Each loader opens it, reads its keys, then closes it. */
extern Preferences prefs;

/**
 * Reads a number from NVS.
 *
 * @param[in] key       NVS key, at most 15 chars.
 * @param[in] fallback  Value returned when the key was never set.
 * @return The stored value, or @p fallback.
 */
uint32_t readU32(const char *key, uint32_t fallback);

/**
 * Reads a string from NVS into a fixed size buffer.
 *
 * The result is always null terminated, and truncated if it does not fit.
 *
 * @param[in]  key       NVS key, at most 15 chars.
 * @param[in]  fallback  String copied when the key was never set.
 * @param[out] out       Destination buffer.
 * @param[in]  outLen    Size of @p out in bytes.
 */
void readStr(const char *key, const char *fallback, char *out, size_t outLen);

/**
 * Reads a MAC address stored as text ("aa:bb:cc:dd:ee:ff") from NVS.
 *
 * @param[in]  key       NVS key, at most 15 chars.
 * @param[in]  fallback  MAC copied when the key is missing or malformed.
 * @param[out] out       Destination for the 6 byte MAC.
 */
void readMac(const char *key, const uint8_t fallback[6], uint8_t out[6]);

/**
 * Loads the LoRa radio parameters shared by the substation and gateway.
 *
 * @return Radio settings from NVS and defaults, with the LilyGo board's pins.
 */
LoRaConfig loadLoRaConfig();

/**
 * Loads the LoRa ACK and retry settings shared by the substation and gateway.
 *
 * @return Link settings from NVS and defaults.
 */
LoRaLinkConfig loadLoRaLinkConfig();

#endif
