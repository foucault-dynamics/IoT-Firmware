# `shared_payload`

The on-air frame layout, shared by every device in the chain. Used as a reference point throughout the whole network.

## The path the bytes travel

```text
meter node --ESP-NOW--> substation --LoRa--> gateway --MQTT--> broker
```

The same struct is copied onto the air at each hop. There is no serialisation
step: the bytes of `payload_t` are the packet.

## The two frames

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t uid;           /* node/device ID */
    uint32_t seq;           /* sequence number, for deduplication */
    float    kwh_import;    /* OBIS 1.8.0 */
    float    kwh_export;    /* OBIS 2.8.0 */
    float    voltage;       /* grid voltage */
    float    battery_v;     /* node battery level */
    uint8_t  community_id;
    uint8_t  unit_id;
} payload_t;                /* 26 bytes */

typedef struct {
    uint32_t uid;
    uint32_t seq;
} ack_payload_t;            /* 8 bytes */
#pragma pack(pop)
```

`payload_t` is a meter reading travelling upstream. `ack_payload_t` is the
gateway acknowledging one, travelling back down the LoRa link. The substation
retries a reading until it sees an ACK carrying the matching `uid` and `seq`.

## The compile-time assertions

There are compile-time assertions so any change in network architecture should take this into account and change them accordingly.

## Changing the format

Any change to this header changes the protocol. All three firmwares must be
rebuilt and reflashed together; a device still running the old layout will
misparse every frame from a device running the new one.
