#include "lora.h"

#include <cstdint>
#include <stdint.h>
#include <cstring>
#include <iostream>

LoRa::LoRa(
    uint32_t loraBand,
    uint32_t signalBandwidth,
    uint8_t loraSpreadingFactor,
    uint8_t syncWord,
    uint8_t txPower)
    : loraBand(loraBand),
      signalBandwidth(signalBandwidth),
      loraSpreadingFactor(loraSpreadingFactor),
      syncWord(syncWord),
      txPower(txPower)
{
    std::memset(macAddress, 0, sizeof(macAddress));
}

int LoRa::begin()
{
    // Initialize the hardware:
    // Might include:
    // 1. Set operating frequency
    // 2. Set signal bandwidth
    // 3. Set spreading factor
    // 4. Set sync word
    // 5. Set TX power
    return 0;
}

int LoRa::send(const Payload &payload)
{
    // Serializ packer received into byte buffer
    // Transmit the packet to gateway
    return 0;
}

int LoRa::receive(Payload &payload)
{
    // Check if LoRa packet has been received
    // Authenticate the packet size and sender
    // Deserialize the bytes into SharedPayload
    return 0;
}
