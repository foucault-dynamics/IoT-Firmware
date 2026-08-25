#include "module.h"

#include <cstddef>
#include <cstdint>

#ifndef LORA_MODULE_H
#define LORA_MODULE_H

class LoRaModule : public Module
{
private:
    uint32_t loraBand;
    uint32_t signalBandwidth;
    uint8_t loraSpreadingFactor;
    uint8_t syncWord;
    uint8_t txPower;
    uint8_t macAddress[6];

public:
    LoRaModule(
        uint32_t loraBand,
        uint32_t signalBandwidth,
        uint8_t loraSpreadingFactor,
        uint8_t syncWord,
        uint8_t txPower);

    void init() override;

    int send(const uint8_t *data, size_t len) override;

    int readByte() override;

    bool available() override;
};

#endif
