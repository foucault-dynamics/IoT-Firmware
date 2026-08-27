#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "shared_payload.h"
#include "node_config.h"
#include "iec62056_21.h"

#ifdef USE_SIMULATED_METER
#include "simulated_ir_head.h"
#else
#include "real_ir_head.h"
#endif

// config
#include "secrets.h"

// ==========================================
// IR meter (IEC 62056-21 over the optical port)
// ==========================================
IrConfig irConfig = loadIrConfig();

#ifdef USE_SIMULATED_METER
SimulatedIrHead irHead;
#else
RealIrHead irHead(irConfig, Serial1);
#endif
Iec6205621Reader meter(irHead);

// ==========================================
// 1. Substation (LilyGo) MAC Address
// ==========================================
// Replace with the actual MAC address of your LilyGo Substation
uint8_t broadcastAddress[] = SECRET_MAC; 

// Map "MINI-002" to an integer UID to save bandwidth
const uint32_t DEVICE_UID = 2; //Hardcoded
#define SLEEP_TIME_SEC 30

// ==========================================
// 2. Core Architecture: Binary Buffer (RTC Memory)
// ==========================================

#define MAX_BUFFER_SIZE 15 // Store up to 15 offline records
RTC_DATA_ATTR Payload readingBuffer[MAX_BUFFER_SIZE];
RTC_DATA_ATTR int bufferCount = 0; // Number of unsent records currently queued

RTC_DATA_ATTR float cumulativeImport = 5000.0;
RTC_DATA_ATTR uint32_t messageCounter = 0; // The SEQ number

// ==========================================
// 3. State Machine Variables (for waiting ACK)
// ==========================================
volatile bool ackReceived = false;
volatile bool deliverySuccess = false;

// ESP-NOW send result callback (interrupt function)
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  deliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
  ackReceived = true; // Notify main loop: ACK received
}

void setup() {
  Serial.begin(115200);
  delay(3000); // For debugging logs during development, remove in production
  Serial.println("\n\n--- SuperMini Fault-Tolerant Wake ---");  

  unsigned long startTime = millis(); 

  // 3. Initialize RF antenna and ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
      Serial.println("ESP-NOW Init Failed");
      return;
  }
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
      return;
  }

  // 4. Read the meter over IR
  meter.setup();

  // Handshake only, for now -- confirms the wake-up/negotiation step works
  // in isolation before the data-block read (meter.poll()) gets layered
  // back on top of it.
  uint32_t negotiatedBaud = 0;
  int handshakeResult = meter.handshake(negotiatedBaud);
  if (handshakeResult == 0) {
    Serial.printf("IR handshake OK — negotiated %u baud\n", negotiatedBaud);
  } else {
    Serial.printf("IR handshake failed (code %d)\n", handshakeResult);
  }

  // meter.poll() (wake-up + data-block read + OBIS parse) is temporarily
  // not called here -- it duplicates the wake-up handshake() now does
  // internally, and running both back to back against SimulatedIrHead
  // would double-drive its state machine. Left for the next step:
  // reconciling poll() to call handshake() instead of repeating it.
  //
  // Payload reading = {};
  // reading.uid = DEVICE_UID;
  // reading.seq = messageCounter++;
  // if (meter.poll(reading) == 0) { ... }

  // NOTE: nothing below this actually calls esp_now_send() yet — that was
  // already missing before this change. Not touching it; out of scope for
  // the IR module.
}

void loop() {

}
