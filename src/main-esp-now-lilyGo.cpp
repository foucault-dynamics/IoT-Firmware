#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LoRa.h>
#include "SharedPayload.h" 

// config
#include "secrets.h"

// --- LoRa & OLED Pins ---
#define SCK 4
#define MISO 5
#define MOSI 6
#define SS 7
#define RST 3
#define DIO0 1
#define LORA_BAND SECRET_LORA_BAND

// --- Interrupt-Safe Variables ---
volatile bool hasNewDataToRelay = false;
Payload pendingPayload;

// --- ESP-NOW Callback ---
// This function only handles "receiving", not "sending" or "waiting"
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    if(len != sizeof(Payload)) {
        Serial.println("[Error] Payload size mismatch!");
        return;
    }
    // Copy data to global variable and set a flag to notify loop() to process it
    memcpy((uint8_t*)&pendingPayload, incomingData, sizeof(pendingPayload));
    hasNewDataToRelay = true; 
}

// --- Core Logic for LoRa Transmission and Waiting for ACK ---
bool sendLoRaWithAck(Payload data) {
    int maxRetries = 3;             // Maximum number of retries
    unsigned long timeoutMs = 1500; // Timeout for waiting for ACK (1.5 seconds)

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        Serial.printf("\n[LoRa] Tx Attempt %d/%d | UID: %d | SEQ: %d\n", attempt, maxRetries, data.uid, data.seq);
        
        // 1. Send data
        LoRa.beginPacket();
        LoRa.write((uint8_t*)&data, sizeof(data));
        LoRa.endPacket();

        // 2. Immediately switch to receive mode, prepare to listen for Gateway's ACK
        LoRa.receive(); 
        
        unsigned long startTime = millis();
        bool ackReceived = false;

        // 3. Continuously check for received packets within the Timeout period
        while (millis() - startTime < timeoutMs) {
            int packetSize = LoRa.parsePacket();
            
            if (packetSize == sizeof(AckPayload)) {
                AckPayload ack;
                LoRa.readBytes((uint8_t*)&ack, sizeof(ack));
                
                // Check if this ACK is for the data we just sent
                if (ack.uid == data.uid && ack.seq == data.seq) {
                    ackReceived = true;
                    break; // Successfully received, exit the waiting loop
                }
            }
        }

        // 4. Evaluate the result
        if (ackReceived) {
            Serial.println("[LoRa] TX SUCCESS: ACK Received!");
            return true; // Mission accomplished
        } else {
            Serial.println("[LoRa] TX FAILED: ACK Timeout.");
            delay(500); // Wait briefly before retrying to avoid band congestion
        }
    }

    Serial.println("[LoRa] TX CRITICAL: Max retries reached. Data dropped.");
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(5000);

    SPI.begin(SCK, MISO, MOSI, SS);
    LoRa.setPins(SS, RST, DIO0);
    if (!LoRa.begin(LORA_BAND)) {
	Serial.printf("ERROR: LoRa Init Failed\n");
        ESP.restart();
    }
    
    // LoRa Optimization for Broadcast
    LoRa.setSpreadingFactor(10); 
    LoRa.setSignalBandwidth(125E3);
    LoRa.setSyncWord(0xF3);
    LoRa.enableCrc();
    LoRa.setTxPower(14);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.printf("[Error] ESP-NOW Init Failed\n");
        return;
    }

    //Function triggered at ESP-NOW reception
    esp_now_register_recv_cb(OnDataRecv);
    
    String fullMac = WiFi.macAddress();
    Serial.println(">>> MAC Address: " + fullMac + " <<<");
    Serial.println("SUBSTATION: Ready to Relay\n");    
}

void loop() {
    // Triggered when ESP-NOW receives new data
    if (hasNewDataToRelay) {
        hasNewDataToRelay = false; // Reset the flag
       
	Serial.printf("RELAYING UID: %s SEQ: %s",String(pendingPayload.uid).c_str(),String(pendingPayload.seq).c_str());
        
        // Execute transmission and retry logic
        bool success = sendLoRaWithAck(pendingPayload);
        
        if (success) {
	    Serial.printf("RELAY SUCCESS UID = %s ACK RECEIVED",String(pendingPayload.uid).c_str());
        } else {
	    Serial.printf("RELAY FAILED UID = %s: Timeout Dropped", String(pendingPayload.uid).c_str());
        }
    }
}
