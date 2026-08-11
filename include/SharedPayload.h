#ifndef SHARED_PAYLOAD_H
#define SHARED_PAYLOAD_H
#include <stdint.h>

#pragma pack(push, 1) 
struct Payload {
    uint32_t uid;           // 4 bytes: Node/Device ID
    uint32_t seq;           // 4 bytes: Sequence number for deduplication handling
    float kwh_import;       // 4 bytes: 1.8.0 value
    float kwh_export;       // 4 bytes: 2.8.0 value
    float voltage;          // 4 bytes: Grid voltage
    float battery_v;        // 4 bytes: ESP32 battery level
    uint8_t community_id;   // 1 byte: Community code
    uint8_t unit_id;        // 1 byte: Unit code
} typedef Payload; 

struct AckPayload {
    uint32_t uid;
    uint32_t seq;
} typedef AckPayload;
#pragma pack(pop)

#endif 
