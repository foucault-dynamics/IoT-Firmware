/*
 * Wire format shared by every node in the NEXTGEN IEMS chain.
 *
 * The same bytes travel: meter node --ESP-NOW--> substation --LoRa--> gateway --MQTT--> broker.
 * All ESP32 targets used here (Xtensa and RISC-V) are little-endian and use
 * IEEE-754 binary32 floats, so the struct can be memcpy'd onto the air directly.
 *
 * If this file changes, every firmware must be rebuilt and reflashed.
 */
#ifndef SHARED_PAYLOAD_H
#define SHARED_PAYLOAD_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t uid;           /* 4 bytes: node/device ID */
    uint32_t seq;           /* 4 bytes: sequence number for deduplication */
    float    kwh_import;    /* 4 bytes: OBIS 1.8.0 value */
    float    kwh_export;    /* 4 bytes: OBIS 2.8.0 value */
    float    voltage;       /* 4 bytes: grid voltage */
    float    battery_v;     /* 4 bytes: node battery level */
    uint8_t  community_id;  /* 1 byte: community code */
    uint8_t  unit_id;       /* 1 byte: unit code */
} payload_t;

typedef struct {
    uint32_t uid;
    uint32_t seq;
} ack_payload_t;
#pragma pack(pop)

/*
 * The receivers demultiplex data frames from ACK frames purely by length, so
 * the two sizes must stay distinct and must stay stable across firmwares.
 */
_Static_assert(sizeof(payload_t) == 26, "payload_t must stay 26 bytes on the wire");
_Static_assert(sizeof(ack_payload_t) == 8, "ack_payload_t must stay 8 bytes on the wire");
_Static_assert(sizeof(payload_t) != sizeof(ack_payload_t),
               "data and ACK frames are told apart by length, so they must differ");

#endif /* SHARED_PAYLOAD_H */
