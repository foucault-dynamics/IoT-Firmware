#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SharedPayload.h" 
// config
#include "secrets.h"

// --- Network & MQTT Config ---
const bool ENABLE_MQTT = true;

const char* ssid = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASS;
const char* mqtt_server = SECRET_MQTT_SERVER; 
const int mqtt_port = SECRET_MQTT_PORT;

const char* mqtt_topic = SECRET_MQTT_TOPIC;

// --- LoRa & OLED Pins ---
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DIO0 26
#define OLED_SDA 21
#define OLED_SCL 22
#define LORA_BAND SECRET_LORA_BAND 

Adafruit_SSD1306 display(128, 64, &Wire, -1);
WiFiClient espClient;
PubSubClient client(espClient);

// --- Interrupt Safe Variables (ISR) ---
volatile bool newLoRaPacket = false;
volatile bool sizeMismatchError = false; 
volatile int errorPacketSize = 0;        

Payload globalPayload;
volatile int globalRssi = 0;
volatile float globalSnr = 0.0;
volatile int globalPacketSize = 0;

// --- Display Helper ---
void updateScreen(String title, String line1, String line2) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("=== " + title + " ===");
    display.println(line1);
    display.println(line2);
    display.display();
}

// --- Network Setup ---
void setup_wifi() {
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

void reconnect_mqtt() {
    while (!client.connected()) {
        String clientId = "LilyGo-Gateway-" + String(random(0xffff), HEX);
        if (client.connect(clientId.c_str())) {
            Serial.println("[MQTT] Connected to Broker");
            updateScreen("GATEWAY", "MQTT Connected", "Waiting for LoRa...");
        } else {
            Serial.print("[MQTT] Connection Failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying in 5 seconds...");
            delay(5000);
        }
    }
}

// --- LoRa Receive Interrupt ---
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
void setup() {
    Serial.begin(115200);
    for(int i = 3; i > 0; i--) {
        Serial.printf("[System] Starting in %d...\n", i);
        delay(1000);
    }

    Serial.println("\n--- Gateway Substation Booting ---");


    Wire.begin(OLED_SDA, OLED_SCL);
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        display.setTextSize(1);
        display.setTextColor(WHITE);
        updateScreen("GATEWAY", "Booting...", "");
        Serial.println("[System] OLED Initialized.");
    }

    if (ENABLE_MQTT) {
        setup_wifi();
        client.setServer(mqtt_server, mqtt_port);
    } else {
        Serial.println("[System] MQTT & WiFi is DISABLED.");
    }

    SPI.begin(SCK, MISO, MOSI, SS);
    LoRa.setPins(SS, RST, DIO0);
    if (!LoRa.begin(LORA_BAND)) {
        updateScreen("ERROR", "LoRa Init Failed", "");
        Serial.println("[Critical Error] LoRa Initialization Failed!");
        while (1);
    }
    LoRa.setSpreadingFactor(10);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setSyncWord(0xF3);
    LoRa.enableCrc();
    LoRa.setTxPower(14);


    Serial.println("[System] Gateway Ready. Mode: Polling Loop.");
    updateScreen("GATEWAY", "Ready", "Listening LoRa...");
    Serial.println("----------------------------------------------");
}

// --- Main Loop ---
void loop() {
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

            updateScreen("LORA RX OK", "UID: " + String(globalPayload.uid), "RSSI: " + String(rssi));


            AckPayload ack;
            ack.uid = globalPayload.uid;
            ack.seq = globalPayload.seq;

            Serial.println("[LoRa TX ACK] >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
            Serial.printf("=> Sending ACK for UID: %u | SEQ: %u\n", ack.uid, ack.seq);
            

            delay(10); 
            LoRa.beginPacket();
            LoRa.write(&ack, sizeof(AckPayload));
            LoRa.endPacket();
            
            Serial.println("=> ACK Sent Successfully!");

   
            if (ENABLE_MQTT && client.connected()) {
                Serial.println("[MQTT TX] >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
                bool pubSuccess = client.publish(mqtt_topic, (const uint8_t*)&globalPayload, sizeof(globalPayload));
                if (pubSuccess) {
                    Serial.println("=> MQTT Publish SUCCESS!");
                    updateScreen("DATA FWD", "UID: " + String(globalPayload.uid), "Pub: SUCCESS");
                } else {
                    Serial.println("=> MQTT Publish FAILED!");
                    updateScreen("DATA FWD", "UID: " + String(globalPayload.uid), "Pub: FAILED");
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
            updateScreen("RX ERROR", "Size Mismatch", "Got: " + String(packetSize) + "B");
        }
    }

    delay(1); 
}
