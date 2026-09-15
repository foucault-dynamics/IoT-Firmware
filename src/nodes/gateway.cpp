#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "loramodule.h"
#include "lora_link.h"
#include "nodes.h"
#include "node_config.h"
#include "nvs_config.h"
#include "shared_payload.h"

// Gateway node: receives Payloads over LoRa (ACK handled inside LoRaLink)
// and publishes the raw payload bytes to MQTT.

static LoRaModule *radio = nullptr;
static LoRaLink *loraLink = nullptr;

static GatewayConfig cfg;

static WiFiClientSecure espClient;
static PubSubClient client(espClient);

static Payload payload;
static bool ready = false;

static void setup_wifi() {
  delay(10);
  Serial.printf("[WiFi] Connecting to %s", cfg.wifi.ssid);
  WiFi.begin(cfg.wifi.ssid, cfg.wifi.password);

  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++counter > 20) {
      Serial.println("\n[WiFi] Connect Timeout!");
      return;
    }
  }

  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());
}

static void reconnect_mqtt() {
  while (!client.connected()) {
    String clientId = "LilyGo-Gateway-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), cfg.mqtt.username, cfg.mqtt.password)) {
      Serial.println("[MQTT] Connected to Broker");
      Serial.println("GATEWAY MQTT CONNECTED Waiting for LoRa...");
    } else {
      Serial.print("[MQTT] Connection Failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void gatewaySetup() {
  cfg = loadGatewayConfig();

  radio = new LoRaModule(cfg.lora);
  loraLink = new LoRaLink(*radio, cfg.link);

  if (cfg.mqtt.enabled) {
    setup_wifi();
    espClient.setInsecure();  // TODO: use a real CA instead of skipping validation.
    client.setServer(cfg.mqtt.server, cfg.mqtt.port);
  }

  if (loraLink->init() != EXIT_SUCCESS) {
    Serial.println("[Gateway] LoRa init failed, idling");
    return;
  }

  Serial.println("[System] Gateway Ready. Mode: Polling Loop.");
  ready = true;
}

void gatewayLoop() {
  if (!ready) {
    return;
  }

  if (cfg.mqtt.enabled) {
    if (!client.connected()) {
      reconnect_mqtt();
    }
    client.loop();
  }

  int len = loraLink->receivePacket(nullptr, reinterpret_cast<uint8_t *>(&payload), sizeof(payload));
  if (len != sizeof(Payload)) {
    return;
  }

  int rssi = radio->packetRssi();
  float snr = radio->packetSnr();
  Serial.printf("=> Packet Size: %d Bytes | RSSI: %d dBm | SNR: %.1f dB\n", len, rssi, snr);
  Serial.printf("=> Parsed Data -> UID: %u | SEQ: %u | Volt: %.1fV\n", payload.uid, payload.seq, payload.voltage);

  if (cfg.mqtt.enabled && client.connected()) {
    bool pubSuccess = client.publish(cfg.mqtt.topic, reinterpret_cast<const uint8_t *>(&payload), sizeof(payload));
    Serial.printf("DATA FWD, UID: %u, PUB: %s\n", payload.uid, pubSuccess ? "SUCCESS" : "FAILED");
  }
}
