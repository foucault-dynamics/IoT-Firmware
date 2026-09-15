#include "esp_now_uplink.h"
#include "esp32-hal.h"
#include "esp_now.h"
#include "esp_wifi_types.h"
#include "freertos/projdefs.h"
#include "node_config.h"
#include "wifi_radio.h"
#include <WiFi.h>
#include <cstddef>
#include <cstdint>
#include <esp_wifi.h>
#include <cstring>
#include <cstdlib>

// EspNowUplink.h instance
EspNowUplink *EspNowUplink::instance = nullptr;

// Constructor
EspNowUplink::EspNowUplink(EspNowConfig config){
  this->config = config;
}

// Automatically Run after send is called for ESP NOW
void EspNowUplink::onSent(const uint8_t *mac, esp_now_send_status_t status){
  if(instance == nullptr){
    return;
  }
  instance->deliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
  xSemaphoreGive(instance->sendDone);
}

// When packet received
void EspNowUplink::onReceived(const uint8_t *mac, const uint8_t *data, int len){
  // Not initialised
  if(instance == nullptr){
    return;
  }

  RxFrame frame;
  memcpy(frame.mac,mac,ESP_NOW_ETH_ALEN);
  size_t copyLen = (len > ESP_NOW_MAX_DATA_LEN)
    ? ESP_NOW_MAX_DATA_LEN :
    static_cast<size_t>(len);
  memcpy(frame.data,data,copyLen);
  frame.len = copyLen;
  xQueueSend(instance->rxQueue,&frame,0);
}

// Initialisation of ESP-NOW transmission
int EspNowUplink::init(){

  // The radio (lib/wifi_radio) must already have brought the AP up.
  if(!wifiRadioUp()){
    Serial.println("[EspNowUplink] Radio is not up");
    return EXIT_FAILURE;
  }

  // Create Receive Queue
  rxQueue = xQueueCreate(ESPNOW_RX_QUEUE_DEPTH,sizeof(RxFrame));
  if(rxQueue == nullptr){
    Serial.println("[EspNowUplink] Failed to create RX queue");
    return EXIT_FAILURE;
  }

  // Create send Semaphore
  sendDone = xSemaphoreCreateBinary();
  if(sendDone == nullptr){
    Serial.println("[EspNowUplink] Failed to create send semaphore");
    return EXIT_FAILURE;
  }

  // Initialise ESP_NOW
  if(esp_now_init() != ESP_OK){
    Serial.println("[EspNowUplink] Failed to init ESP-NOW");
    return EXIT_FAILURE;
  }

  instance = this;

  // Register Send and Receive callbacks
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceived);

  return EXIT_SUCCESS;
}


// Add to list of registered Peers
int EspNowUplink::addPeer(EspNowPeerConfig peer){

  esp_now_peer_info_t info{};
  // Mac address of peer
  memcpy(info.peer_addr,peer.mac,ESP_NOW_ADDRESS_LEN);
  // 0 means the peer follows the radio's current channel.
  info.channel = 0;

  // The radio always runs as an AP.
  info.ifidx = WIFI_IF_AP;

  if(esp_now_add_peer(&info) != ESP_OK){
    Serial.println("[EspNowUplink] Failed to add peer");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;

}

// Trigger send of packet
int EspNowUplink::sendPacket(const void *address, const uint8_t *buf, size_t len){
  // Validate passed len
  if(len == 0 || len > ESP_NOW_MAX_DATA_LEN){
    Serial.println("[EspNowUplink] Invalid packet length");
    return EXIT_FAILURE;
  }

  // Clear for any hanging sends
  xSemaphoreTake(sendDone,0);

  // Send packet
  if(esp_now_send(static_cast<const uint8_t *>(address), buf, len) != ESP_OK){
    Serial.println("[EspNowUplink] esp_now_send failed");
    return EXIT_FAILURE;
  }

  // Hangs until successfull send
  if(xSemaphoreTake(sendDone,pdMS_TO_TICKS(config.sendTimeoutMs)) != pdTRUE){
    Serial.println("[EspNowUplink] Send timeout waiting for delivery ACK");
    return EXIT_FAILURE;
  }

  // If delivery was not successfull
  if(!deliverySuccess){
    Serial.println("[EspNowUplink] Delivery failed");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;

}

// Receive a packet from queue (returns len received)
int EspNowUplink::receivePacket(void *address, uint8_t *buf, size_t bufLen){

  // Take out of queue
  RxFrame frame;
  if(xQueueReceive(rxQueue,&frame,0) != pdTRUE){
    return -1;
  }

  // Frame can't be transferred to buffer due to length
  if(frame.len > bufLen){
    Serial.println("[EspNowUplink] Receive buffer too small, frame dropped");
    return -2;
  }

  // If address not specified
  if(address != nullptr){
    memcpy(address,frame.mac,ESP_NOW_ADDRESS_LEN);
  }
  memcpy(buf,frame.data,frame.len);

  return frame.len;

}

