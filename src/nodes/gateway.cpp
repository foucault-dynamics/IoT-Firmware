// Gateway node: receives Payloads over LoRa, replies with an ACK, and
// publishes the raw payload bytes to MQTT.
// Verbatim port of src/main-esp-now-gateway.cpp (kept as legacy reference);
// only changes: setup()/loop() renamed, file-scope symbols made static,
// Serial.begin moved to src/main.cpp.

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LoRa.h>
#include <cstdint>
#include "shared_payload.h"
#include "nodes.h"
// config
#include "secrets.h"

// --- Network & MQTT Config ---
static const bool ENABLE_MQTT = true;

static const char* ssid = SECRET_WIFI_SSID;
static const char* password = SECRET_WIFI_PASS;
static const char* mqtt_server = SECRET_MQTT_SERVER;
static const int mqtt_port = SECRET_MQTT_PORT;

static const char* mqtt_topic = SECRET_MQTT_TOPIC;

// --- LoRa Pins ---
#define SCK 4
#define MISO 5
#define MOSI 6
#define SS 7
#define RST 3
#define DIO0 1
#define LORA_BAND SECRET_LORA_BAND

static WiFiClient espClient;
static PubSubClient client(espClient);

// --- Interrupt Safe Variables (ISR) ---
static volatile bool newLoRaPacket = false;
static volatile bool sizeMismatchError = false;
static volatile int errorPacketSize = 0;

static Payload globalPayload;
static volatile int globalRssi = 0;
static volatile float globalSnr = 0.0;
static volatile int globalPacketSize = 0;

// --- Network Setup ---
static void setup_wifi() {
    delay(10);
    WiFi.begin(ssid, password);
    int counter = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        counter++;
        if(counter > 20) {
            Serial.println("\n[WiFi] Connect Timeout! Retrying...");
            break;
        }
    }
    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Connected!");
        IPAddress dns(8, 8, 8, 8);
        WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns);
        Serial.println("[WiFi] DNS forced to 8.8.8.8");

    }
}

static void reconnect_mqtt() {
    while (!client.connected()) {
        String clientId = "LilyGo-Gateway-" + String(random(0xffff), HEX);
        if (client.connect(clientId.c_str())) {
            Serial.println("[MQTT] Connected to Broker");
	    Serial.printf("GATEWAY MQTT CONNECTED Waiting for LoRa...\n");
        } else {
            Serial.print("[MQTT] Connection Failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying in 5 seconds...");
            delay(5000);
        }
    }
}

// --- LoRa Receive Interrupt ---
// Kept from the original sketch even though it is never registered; the loop
// below polls instead.
void onLoRaReceive(int packetSize) {
    if (packetSize == 0) return;

    if (packetSize == sizeof(Payload)) {
        // Valid Data Payload: Read and trigger processing
        LoRa.readBytes((uint8_t*)&globalPayload, sizeof(globalPayload));
        globalRssi = LoRa.packetRssi();
        globalSnr = LoRa.packetSnr();
        globalPacketSize = packetSize;
        newLoRaPacket = true;
    }
    else if (packetSize == sizeof(AckPayload)) {
        // Silently ignore ACK packets meant for other nodes to prevent spamming errors
        return;
    }
    else {
        // Invalid size: Trigger error flag for main loop
        errorPacketSize = packetSize;
        sizeMismatchError = true;
    }
}

// --- Initialization ---
void gatewaySetup() {
    for(int i = 3; i > 0; i--) {
        Serial.printf("[System] Starting in %d...\n", i);
        delay(1000);
    }

    Serial.println("\n--- Gateway Substation Booting ---");

    if (ENABLE_MQTT) {
        setup_wifi();
        client.setServer(mqtt_server, mqtt_port);
    } else {
        Serial.println("[System] MQTT & WiFi is DISABLED.");
    }

    SPI.begin(SCK, MISO, MOSI, SS);
    LoRa.setPins(SS, RST, DIO0);
    if (!LoRa.begin(LORA_BAND)) {
        Serial.println("[Critical Error] LoRa Initialization Failed!");
        while (1);
    }
    LoRa.setSpreadingFactor(10);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setSyncWord(0xF3);
    LoRa.enableCrc();
    LoRa.setTxPower(14);


    Serial.println("[System] Gateway Ready. Mode: Polling Loop.");

    Serial.println("----------------------------------------------");
}

// --- Main Loop ---
void gatewayLoop() {
    // 1. Maintain MQTT Connection
    if (ENABLE_MQTT) {
        if (!client.connected()) {
            reconnect_mqtt();
        }
        client.loop();
    }

    // Key change 6: safe polling logic (replaces original interrupt-based approach)
    int packetSize = LoRa.parsePacket();

    if (packetSize > 0) {
        if (packetSize == sizeof(Payload)) {
            // Received data packet with correct size
            LoRa.readBytes((uint8_t*)&globalPayload, sizeof(globalPayload));
            int rssi = LoRa.packetRssi();
            float snr = LoRa.packetSnr();

            Serial.println("\n[LoRa RX] <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
            Serial.printf("=> Packet Size: %d Bytes | RSSI: %d dBm | SNR: %.1f dB\n", packetSize, rssi, snr);
            Serial.printf("=> Parsed Data -> UID: %u | SEQ: %u | Volt: %.1fV\n", globalPayload.uid, globalPayload.seq, globalPayload.voltage);


            AckPayload ack;
            ack.uid = globalPayload.uid;
            ack.seq = globalPayload.seq;

            Serial.println("[LoRa TX ACK] >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
            Serial.printf("=> Sending ACK for UID: %u | SEQ: %u\n", ack.uid, ack.seq);

            delay(10);
            LoRa.beginPacket();
            LoRa.write((uint8_t *)&ack, sizeof(AckPayload));
            LoRa.endPacket();

            Serial.println("=> ACK Sent Successfully!");


            if (ENABLE_MQTT && client.connected()) {
                Serial.println("[MQTT TX] >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
                bool pubSuccess = client.publish(mqtt_topic, (const uint8_t*)&globalPayload, sizeof(globalPayload));
                if (pubSuccess) {
                    Serial.println("=> MQTT Publish SUCCESS!");
		    Serial.printf("DATA FWD, UID: %s, PUB: SUCCESS\n",String(globalPayload.uid).c_str());
                } else {
                    Serial.println("=> MQTT Publish FAILED!");
		    Serial.printf("DATA FWD, UID: %s, PUB: FAILED\n",String(globalPayload.uid).c_str());
                }
            }
            Serial.println("----------------------------------------------");

        }
        else if (packetSize == sizeof(AckPayload)) {

        }
        else {

            Serial.println("\n[LoRa RX ERROR] ==============================");
            Serial.printf("=> Packet Size Mismatch! Expected: %d | Got: %d\n", sizeof(Payload), packetSize);
            Serial.println("==============================================\n");
	    Serial.printf("RX ERROR, Size Mismatch, Got: %sB",String(packetSize).c_str());
        }
    }

    delay(1);
}
