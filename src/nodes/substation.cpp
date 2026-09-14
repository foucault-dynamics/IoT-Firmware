// Substation node: receives Payloads from meter nodes over ESP-NOW and
// relays them over LoRa with an ACK/retry scheme.
// Verbatim port of src/main-esp-now-lilyGo.cpp (kept as legacy reference);
// only changes: setup()/loop() renamed, file-scope symbols made static,
// Serial.begin moved to src/main.cpp.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "shared_payload.h"
#include "nodes.h"
#include "loramodule.h"

// config
#include "secrets.h"

// // --- LoRa & OLED Pins ---
// #define SCK 4
// #define MISO 5
// #define MOSI 6
// #define SS 7
// #define RST 3
// #define DIO0 1
#define LORA_BAND SECRET_LORA_BAND

static LoRaModule radio(
    LORA_BAND,
    125E3,
    10,
    0xF3,
    14);
// --- Interrupt-Safe Variables ---
static volatile bool hasNewDataToRelay = false;
static Payload pendingPayload;

// --- ESP-NOW Callback ---
// This function only handles "receiving", not "sending" or "waiting"
static void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    if (len != sizeof(Payload))
    {
        Serial.println("[Error] Payload size mismatch!");
        return;
    }
    // Copy data to global variable and set a flag to notify loop() to process it
    memcpy((uint8_t *)&pendingPayload, incomingData, sizeof(pendingPayload));
    hasNewDataToRelay = true;
}

// --- Core Logic for LoRa Transmission and Waiting for ACK ---
static bool sendLoRaWithAck(Payload data)
{
    int maxRetries = 3;             // Maximum number of retries
    unsigned long timeoutMs = 1500; // Timeout for waiting for ACK (1.5 seconds)

    for (int attempt = 1; attempt <= maxRetries; attempt++)
    {
        Serial.printf("\n[LoRa] Tx Attempt %d/%d | UID: %d | SEQ: %d\n", attempt, maxRetries, data.uid, data.seq);

        // // 1. Send data
        // LoRa.beginPacket();
        // LoRa.write((uint8_t*)&data, sizeof(data));
        // LoRa.endPacket();

        int result = radio.send(
            reinterpret_cast<const uint8_t *>(&data),
            sizeof(Payload)

        );
        if (result != EXIT_SUCCESS)
        {
            Serial.println("[LoRa] Failed to send packet");
            continue;
        }

        // 2. Immediately switch to receive mode, prepare to listen for Gateway's ACK
        radio.receive();

        unsigned long startTime = millis();
        bool ackReceived = false;

        // 3. Continuously check for received packets within the Timeout period
        while (millis() - startTime < timeoutMs)
        {
            int packetSize = radio.parsePacket();

            if (packetSize == sizeof(AckPayload))
            {
                AckPayload ack;

                uint8_t *ackBytes =
                    reinterpret_cast<uint8_t *>(&ack);

                size_t bytesRead = 0;

                while (bytesRead < sizeof(AckPayload))
                {
                    int value = radio.readByte();

                    if (value >= 0)
                    {
                        ackBytes[bytesRead++] =
                            static_cast<uint8_t>(value);
                    }
                }

                if (ack.uid == data.uid && ack.seq == data.seq)
                {
                    ackReceived = true;
                    break;
                }
            }
        }

        // 4. Evaluate the result
        if (ackReceived)
        {
            Serial.println("[LoRa] TX SUCCESS: ACK Received!");
            return true; // Mission accomplished
        }
        else
        {
            Serial.println("[LoRa] TX FAILED: ACK Timeout.");
            delay(500); // Wait briefly before retrying to avoid band congestion
        }
    }

    Serial.println("[LoRa] TX CRITICAL: Max retries reached. Data dropped.");
    return false;
}

void substationSetup()
{
    delay(5000);

    // SPI.begin(SCK, MISO, MOSI, SS);
    // LoRa.setPins(SS, RST, DIO0);
    // if (!LoRa.begin(LORA_BAND)) {
    // Serial.printf("ERROR: LoRa Init Failed\n");
    //     ESP.restart();
    // }

    // // LoRa Optimization for Broadcast
    // LoRa.setSpreadingFactor(10);
    // LoRa.setSignalBandwidth(125E3);
    // LoRa.setSyncWord(0xF3);
    // LoRa.enableCrc();
    // LoRa.setTxPower(14);

    radio.init();

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
    {
        Serial.printf("[Error] ESP-NOW Init Failed\n");
        return;
    }

    // Function triggered at ESP-NOW reception
    esp_now_register_recv_cb(OnDataRecv);

    String fullMac = WiFi.macAddress();
    Serial.println(">>> MAC Address: " + fullMac + " <<<");
    Serial.println("SUBSTATION: Ready to Relay\n");
}

void substationLoop()
{
    if (hasNewDataToRelay)
    {
        hasNewDataToRelay = false;

        Serial.printf(
            "\n[Substation] Relaying UID: %u | SEQ: %u\n", pendingPayload.uid, pendingPayload.seq);

        bool success = sendLoRaWithAck(pendingPayload);

        if (success)
        {
            Serial.printf(
                "[Substation] Relay SUCCESS | UID: %u | SEQ: %u | ACK received\n",
                pendingPayload.uid,
                pendingPayload.seq);
        }
        else
        {
            Serial.printf(
                "[Substation] Relay FAILED | UID: %u | SEQ: %u | Data dropped\n",
                pendingPayload.uid,
                pendingPayload.seq);
        }
    }

    // static unsigned long lastSend = 0;
    // static uint32_t sequence = 1;

    // if (millis() - lastSend >= 5000)
    // {
    //     lastSend = millis();

    //     Payload testPayload;

    //     testPayload.uid = 123;
    //     testPayload.seq = sequence++;
    //     testPayload.kwh_import = 10.5;
    //     testPayload.kwh_export = 2.3;
    //     testPayload.voltage = 240.0;
    //     testPayload.battery_v = 3.9;
    //     testPayload.community_id = 1;
    //     testPayload.unit_id = 2;

    //     Serial.printf(
    //         "\n[TEST] Sending UID: %u | SEQ: %u\n",
    //         testPayload.uid,
    //         testPayload.seq);

    //     bool success = sendLoRaWithAck(testPayload);

    //     if (success)
    //     {
    //         Serial.println("[TEST] Payload delivered + ACK received");
    //     }
    //     else
    //     {
    //         Serial.println("[TEST] Transmission failed");
    //     }
}
}