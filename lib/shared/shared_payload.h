#ifndef SHARED_PAYLOAD_H
#define SHARED_PAYLOAD_H
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdint.h>

// Device UID: the 16-byte eFuse unique ID, read at boot by the config loaders.
const size_t UID_LEN = 16;

#pragma pack(push, 1)
struct Payload {
    uint8_t uid[UID_LEN];   // 16 bytes: Node/Device ID
    uint32_t seq;           // 4 bytes: Sequence number for deduplication handling
    float kwh_import;       // 4 bytes: 1.8.0 value
    float kwh_export;       // 4 bytes: 2.8.0 value
    float voltage;          // 4 bytes: Grid voltage
    float battery_v;        // 4 bytes: ESP32 battery level
    uint8_t community_id;   // 1 byte: Community code
    uint8_t unit_id;        // 1 byte: Unit code
};

struct AckPayload {
    uint8_t uid[UID_LEN];
    uint32_t seq;
};
#pragma pack(pop)

const size_t UID_HEX_LEN = UID_LEN * 2 + 1;
inline const char *uidToHex(const uint8_t *uid, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < UID_LEN; i++) {
        out[i * 2] = digits[uid[i] >> 4];
        out[i * 2 + 1] = digits[uid[i] & 0x0F];
    }
    out[UID_LEN * 2] = '\0';
    return out;
}

// True when two UIDs hold the same bytes.
inline bool uidEquals(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, UID_LEN) == 0;
}

#endif
