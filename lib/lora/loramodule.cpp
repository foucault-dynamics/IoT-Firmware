#include <Arduino.h>
#include "lora_module.h"
#include <SPI.h>
#include <LoRa.h>

#include <cstdint>
#include <cstring>
#include <cstdlib>

#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_DIO0 26

LoRaModule::LoRaModule(
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

void LoRaModule::init()
{
    SPI.begin(
        LORA_SCK,
        LORA_MISO,
        LORA_MOSI,
        LORA_CS);

    LoRa.setPins(
        LORA_CS,
        LORA_RST,
        LORA_DIO0);

    if (!LoRa.begin(loraBand))
    {
        Serial.println("LoRa setup failed");
        return;
    }

    LoRa.setSignalBandwidth(signalBandwidth);
    LoRa.setSpreadingFactor(loraSpreadingFactor);
    LoRa.setSyncWord(syncWord);
    LoRa.setTxPower(txPower);

    Serial.println("LoRa initialized successfully");
}

int LoRaModule::send(const uint8_t *data, size_t len)
{
    LoRa.beginPacket();

    LoRa.write(data, len);

    int result = LoRa.endPacket();

    if (result == 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int LoRaModule::readByte()
{
    if (LoRa.available())
    {
        return LoRa.read();
    }

    return -1;
}

bool LoRaModule::available()
{
    if (LoRa.available() > 0)
    {
        return true;
    }

    int packetSize = LoRa.parsePacket();

    return packetSize > 0;
}
