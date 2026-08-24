#include "module.h"
#include <atomic>
#include <cstdint>
#include <stdint.h>
#include <sys/types.h>
#include "shared_payload.h"

#ifndef LORA_H
#define LORA_H

class LoRa : public Module
{

private:
    uint32_t LoRaBand;
    uint32_t signalBandwidth;
    uint8_t loraSpreadingFactor;
    uint8_t syncWord;
    uint8_t txPower;
    uint8_t macAddress[6];

public:
    LoRa(
        uint32_t LoraBand,
        uint32_t signalBandwidth,
        uint8_t loraSpreadingFactor,
        uint8_t syncWord,
        uint8_t txPower);

    int begin();

    int receive(Payload &payload);

    int send(const Payload &payload);
};

#endif
