#include "lora.h"
#include <SPI.h>
#include <LoRa.h>

#include <cstdint>
#include <stdint.h>
#include <cstring>
#include <iostream>

#define LORA_SCK   5
#define LORA_MISO  19
#define LORA_MOSI  27
#define LORA_CS    18
#define LORA_RST   23
#define LORA_DIO0  26

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
    // Serial.begin(9600);
    // LoRa.begin(915E6);

    SPI.begin(
        LORA_SCK,
        LORA_MISO,
        LORA_MOSI,
        LORA_CS
    );

    ::LoRa.setPins(
        LORA_CS,
        LORA_RST,
        LORA_DIO0
    );

    if (!::LoRa.begin(loraBand))
    {   
        Serial.println("set up failed");
        return -1;
    }

    ::LoRa.setSignalBandwidth(signalBandwidth);
    ::LoRa.setSpreadingFactor(loraSpreadingFactor);
    ::LoRa.setSyncWord(syncWord);
    ::LoRa.setTxPower(txPower);

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
