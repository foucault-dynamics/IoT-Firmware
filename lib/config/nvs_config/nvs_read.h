#ifndef NVS_READ_H
#define NVS_READ_H

#include <Preferences.h>

#include <cstddef>
#include <cstdint>

#include "networking_config.h"

extern const char *NVS_NAMESPACE;
extern Preferences prefs;

uint32_t readU32(const char *key, uint32_t fallback);
void readStr(const char *key, const char *fallback, char *out, size_t outLen);
void readMac(const char *key, const uint8_t fallback[6], uint8_t out[6]);

LoRaConfig loadLoRaConfig();
LoRaLinkConfig loadLoRaLinkConfig();

#endif