/**
 * @file
 * Over the air packet formats shared by every node, plus UID helpers.
 *
 * Every node in the network must be built from the same copy of this header.
 * Both the ESP-NOW and LoRa receive paths reject frames whose size does not
 * match `sizeof(Payload)`, so changing a struct here means reflashing every
 * board.
 */

#ifndef SHARED_PAYLOAD_H
#define SHARED_PAYLOAD_H
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdint.h>

/** Length in bytes of a device UID, the ESP32-C3's eFuse unique ID. */
const size_t UID_LEN = 16;

#pragma pack(push, 1)
/**
 * One meter reading, sent from a meter node to the substation and on to the
 * gateway.
 *
 * Packed so the layout is identical on every board and compiler.
 */
struct Payload {
    uint8_t uid[UID_LEN];   ///< Sending node's eFuse UID.
    uint32_t seq;           ///< Per node sequence number, used to deduplicate retries.
    float kwh_import;       ///< Imported energy in kWh (OBIS 1.8.0).
    float kwh_export;       ///< Exported energy in kWh (OBIS 2.8.0).
    float voltage;          ///< Grid voltage in volts.
    uint8_t community_id;   ///< Community the meter belongs to.
    uint8_t unit_id;        ///< Unit within the community.
};

/**
 * Acknowledgement for one Payload, sent back over LoRa by the gateway.
 *
 * Keyed on the UID and sequence number so the substation can match it to the
 * reading it sent.
 */
struct AckPayload {
    uint8_t uid[UID_LEN];  ///< UID copied from the acknowledged Payload.
    uint32_t seq;          ///< Sequence number copied from the acknowledged Payload.
};
#pragma pack(pop)

/** Buffer size for uidToHex(): two hex digits per byte plus the terminator. */
const size_t UID_HEX_LEN = UID_LEN * 2 + 1;

/**
 * Formats a UID as lowercase hex for logs and JSON.
 *
 * @param[in]  uid  UID_LEN bytes to format.
 * @param[out] out  Buffer of at least UID_HEX_LEN chars.
 * @return @p out, so the call can be used inline in a printf.
 */
inline const char *uidToHex(const uint8_t *uid, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < UID_LEN; i++) {
        out[i * 2] = digits[uid[i] >> 4];
        out[i * 2 + 1] = digits[uid[i] & 0x0F];
    }
    out[UID_LEN * 2] = '\0';
    return out;
}

/**
 * Compares two UIDs byte for byte.
 *
 * @param[in] a  First UID, UID_LEN bytes.
 * @param[in] b  Second UID, UID_LEN bytes.
 * @retval true   Same UID.
 * @retval false  Different UIDs.
 */
inline bool uidEquals(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, UID_LEN) == 0;
}

#endif
