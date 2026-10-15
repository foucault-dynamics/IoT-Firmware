/**
 * @file
 * Over the air packet formats shared by every node, plus UID helpers.
 *
 * Every node in the network must be built from the same copy of this header.
 * The receive paths tell frames apart by size (Payload and LinkFrame on
 * ESP-NOW, Payload and AckPayload on LoRa) and reject any other size, so
 * changing a struct here means reflashing every board.
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

/**
 * Discovery handshake frame between a meter node and the substation over
 * ESP-NOW.
 *
 * Kept apart from Payload, which also crosses LoRa to the gateway, and told
 * apart from it by length and #magic. Its size differs from both Payload and
 * AckPayload so a length check alone never confuses them.
 */
struct LinkFrame {
    uint8_t magic[2];      ///< LINK_FRAME_MAGIC, so stray ESP-NOW traffic is ignored.
    uint8_t type;          ///< A LinkFrameType.
    uint32_t epoch;        ///< Substation's random per boot epoch. A change means it rebooted.
    uint8_t uid[UID_LEN];  ///< Join: the node's UID. JoinAck: echoes it. Beacon: all zero.
};
#pragma pack(pop)

/** Bytes every LinkFrame starts with, "KZ". */
const uint8_t LINK_FRAME_MAGIC[2] = {'K', 'Z'};

/** What a LinkFrame asks or answers. */
enum class LinkFrameType : uint8_t {
    Beacon = 1,   ///< Substation to broadcast: "I am here, on this epoch".
    Join = 2,     ///< Node to substation: "add me", carrying the node's UID.
    JoinAck = 3,  ///< Substation to node: "you are joined", echoing the UID.
};

/**
 * Builds a LinkFrame.
 *
 * @param[in] type   What the frame is.
 * @param[in] epoch  Substation's boot epoch. Nodes send back the one they heard.
 * @param[in] uid    UID_LEN bytes, or nullptr for all zero.
 * @return The frame, ready to send.
 */
inline LinkFrame linkFrameMake(LinkFrameType type, uint32_t epoch, const uint8_t *uid) {
    LinkFrame f{};
    memcpy(f.magic, LINK_FRAME_MAGIC, sizeof(f.magic));
    f.type = static_cast<uint8_t>(type);
    f.epoch = epoch;
    if (uid != nullptr) {
        memcpy(f.uid, uid, UID_LEN);
    }
    return f;
}

/**
 * Checks received bytes are a LinkFrame and copies them out.
 *
 * @param[in]  buf  Received bytes.
 * @param[in]  len  Number of bytes in @p buf.
 * @param[out] out  The frame. Untouched on failure.
 * @retval true   @p buf is exactly one LinkFrame with the magic and a known type.
 * @retval false  Wrong length, wrong magic or unknown type.
 */
inline bool linkFrameParse(const uint8_t *buf, int len, LinkFrame &out) {
    if (len != static_cast<int>(sizeof(LinkFrame))) {
        return false;
    }
    LinkFrame f;
    memcpy(&f, buf, sizeof(f));
    if (memcmp(f.magic, LINK_FRAME_MAGIC, sizeof(f.magic)) != 0) {
        return false;
    }
    if (f.type < static_cast<uint8_t>(LinkFrameType::Beacon)
        || f.type > static_cast<uint8_t>(LinkFrameType::JoinAck)) {
        return false;
    }
    out = f;
    return true;
}

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
