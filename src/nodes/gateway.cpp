/**
 * @file
 * Gateway node: receives readings over LoRa and publishes them to MQTT.
 *
 * Each Payload is ACKed inside LoRaLink, then published as a JSON object with
 * a timestamp and the packet's RSSI and SNR added.
 *
 * A FreeRTOS task receives LoRa and queues readings; the loop publishes them.
 */

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "loramodule.h"
#include "lora_link.h"
#include "nodes.h"
#include "gateway_config.h"
#include "nvs_config.h"
#include "payload_json.h"
#include "shared_payload.h"

/** A received reading and its signal quality. */
struct GatewayReading {
  Payload payload;  ///< Reading.
  int rssi;         ///< RSSI in dBm.
  float snr;        ///< SNR in dB.
};

static LoRaModule *radio = nullptr;   ///< LoRa radio, also asked for RSSI and SNR.
static LoRaLink *loraLink = nullptr;  ///< ACK layer on top of #radio.

static GatewayConfig cfg;  ///< Config loaded at setup.

static WiFiClientSecure espClient;      ///< TLS socket to the broker.
static PubSubClient client{espClient};  ///< MQTT client on top of #espClient.

static QueueHandle_t readings = nullptr;  ///< Readings waiting to be published.
static bool ready = false;                ///< True once setup fully succeeded.

static constexpr UBaseType_t READINGS_DEPTH = 32;     ///< Capacity of #readings.
static constexpr uint32_t LORA_TASK_STACK = 4096;     ///< LoRa task stack in bytes.
static constexpr UBaseType_t LORA_TASK_PRIORITY = 3;  ///< LoRa task priority.
static constexpr uint32_t LORA_POLL_MS = 5;           ///< LoRa poll interval in ms.
static constexpr uint32_t PUBLISH_WAIT_MS = 100;      ///< Queue wait in gatewayLoop() in ms.

static constexpr const char *NTP_SERVER = "pool.ntp.org";  ///< Time source for timestamps.
static constexpr const char *TIMEZONE = "AEST-10";         ///< POSIX timezone: AEST, UTC+10 with no daylight saving.

/** Joins the configured Wi-Fi network, giving up after about 10 s. */
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

/**
 * Connects to the MQTT broker, retrying every 5 s until it succeeds.
 *
 * @warning Blocks until connected, so LoRa packets are not received meanwhile.
 */
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

/**
 * Receives and ACKs LoRa readings and queues them. Stops receiving while the queue is full.
 *
 * @param[in] arg  Unused.
 */
static void loraRxTask(void *arg) {
  (void)arg;
  GatewayReading r;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(LORA_POLL_MS));
    if (uxQueueSpacesAvailable(readings) == 0) {
      continue;
    }
    int len = loraLink->receivePacket(nullptr, reinterpret_cast<uint8_t *>(&r.payload), sizeof(r.payload));
    if (len != sizeof(Payload)) {
      continue;
    }
    r.rssi = radio->packetRssi();
    r.snr = radio->packetSnr();
    xQueueSend(readings, &r, 0);
  }
}

void gatewaySetup() {
  cfg = loadGatewayConfig();

  radio = new LoRaModule(cfg.lora);
  loraLink = new LoRaLink(*radio, cfg.link);

  if (cfg.mqtt.enabled) {
    setup_wifi();
    configTzTime(TIMEZONE, NTP_SERVER);
    espClient.setInsecure();  // TODO: use a real CA instead of skipping validation.
    client.setServer(cfg.mqtt.server, cfg.mqtt.port);
    client.setBufferSize(512);
  }

  if (loraLink->init() != EXIT_SUCCESS) {
    Serial.println("[Gateway] LoRa init failed, idling");
    return;
  }

  readings = xQueueCreate(READINGS_DEPTH, sizeof(GatewayReading));
  if (readings == nullptr) {
    Serial.println("[Gateway] Queue create failed, idling");
    return;
  }

  if (xTaskCreate(loraRxTask, "lora_rx", LORA_TASK_STACK, nullptr, LORA_TASK_PRIORITY, nullptr) != pdPASS) {
    Serial.println("[Gateway] LoRa task create failed, idling");
    return;
  }

  Serial.println("[System] Gateway Ready. Mode: LoRa task + MQTT loop.");
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

  GatewayReading r;
  if (xQueuePeek(readings, &r, pdMS_TO_TICKS(PUBLISH_WAIT_MS)) != pdTRUE) {
    return;
  }

  Serial.printf("=> Packet Size: %d Bytes | RSSI: %d dBm | SNR: %.1f dB\n", (int)sizeof(Payload), r.rssi, r.snr);
  char uidHex[UID_HEX_LEN];
  Serial.printf("=> Parsed Data -> UID: %s | SEQ: %u | Volt: %.1fV\n", uidToHex(r.payload.uid, uidHex), r.payload.seq, r.payload.voltage);

  if (!cfg.mqtt.enabled) {
    xQueueReceive(readings, &r, 0);
    return;
  }

  if (!client.connected()) {
    return;
  }

  char json[320];
  buildPayloadJson(r.payload, r.rssi, r.snr, time(nullptr), json, sizeof(json));
  Serial.printf("=> JSON: %s\n", json);
  bool pubSuccess = client.publish(cfg.mqtt.topic, json);
  Serial.printf("DATA FWD, UID: %s, PUB: %s\n", uidToHex(r.payload.uid, uidHex), pubSuccess ? "SUCCESS" : "FAILED");
  if (pubSuccess) {
    xQueueReceive(readings, &r, 0);
  }
}
