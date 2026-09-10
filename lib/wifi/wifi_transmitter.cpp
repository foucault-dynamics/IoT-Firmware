#include "wifi_transmitter.h"
#include "esp32-hal.h"
#include "esp_now.h"
#include "esp_wifi_types.h"
#include "freertos/projdefs.h"
#include "node_config.h"
#include <WiFi.h>
#include <cstddef>
#include <cstdint>
#include <esp_wifi.h>
#include <cstring>
#include <cstdlib>

// Wifi.h instance
Wifi *Wifi::instance = nullptr;

// Constructor
Wifi::Wifi(EspNowConfig config){
  this->config = config;
}

// Automatically Run after send is called for ESP NOW
void Wifi::onSent(const uint8_t *mac, esp_now_send_status_t status){
  if(instance == nullptr){
    return;
  }
  instance->deliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
  xSemaphoreGive(instance->sendDone);
}

// When packet received
void Wifi::onReceived(const uint8_t *mac, const uint8_t *data, int len){
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

// Initialisation of Wifi transmission
int Wifi::init(){

  // Initialise wifi as either Access point or Standard
  WiFi.mode(config.useApInterface ? WIFI_AP : WIFI_STA);

  // Configure channel if custom
  if(config.channel != 0){
    if(esp_wifi_set_channel(config.channel,WIFI_SECOND_CHAN_NONE) != ESP_OK){
      Serial.println("[WiFi] Failed to set channel");
      return EXIT_FAILURE;
    }
  }

  // Create Receive Queue
  rxQueue = xQueueCreate(ESPNOW_RX_QUEUE_DEPTH,sizeof(RxFrame));
  if(rxQueue == nullptr){
    Serial.println("[WiFi] Failed to create RX queue");
    return EXIT_FAILURE;
  }

  // Create send Semaphore
  sendDone = xSemaphoreCreateBinary();
  if(sendDone == nullptr){
    Serial.println("[WiFi] Failed to create send semaphore");
    return EXIT_FAILURE;
  }

  // Initialise ESP_NOW
  if(esp_now_init() != ESP_OK){
    Serial.println("[Wifi] Failed to init ESP-NOW");
    return EXIT_FAILURE;
  }

  instance = this;

  // Register Send and Receive callbacks
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceived);

  return EXIT_SUCCESS;
}


// Add to list of registered Peers
int Wifi::addPeer(EspNowPeerConfig peer){
  
  esp_now_peer_info_t info{};
  // Mac address of peer
  memcpy(info.peer_addr,peer.mac,WIFI_ADDRESS_LEN);
  // Agreed channel
  info.channel = peer.channel;

  // Access point or Station
  info.ifidx = peer.useApInterface ? WIFI_IF_AP : WIFI_IF_STA;
  
  if(esp_now_add_peer(&info) != ESP_OK){
    Serial.println("[WiFi] Failed to add peer");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
  
}

// Trigger send of packet
int Wifi::sendPacket(const void *address, const uint8_t *buf, size_t len){
  // Validate passed len
  if(len == 0 || len > ESP_NOW_MAX_DATA_LEN){
    Serial.println("[WiFi] Invalid packet length");
    return EXIT_FAILURE;
  }

  // Clear for any hanging sends
  xSemaphoreTake(sendDone,0);
  
  // Send packet
  if(esp_now_send(static_cast<const uint8_t *>(address), buf, len) != ESP_OK){
    Serial.println("[WiFi] esp_now_send failed");
    return EXIT_FAILURE;
  }

  // Hangs until successfull send
  if(xSemaphoreTake(sendDone,pdMS_TO_TICKS(config.sendTimeoutMs)) != pdTRUE){
    Serial.println("[WiFi] Send timeout waiting for delivery ACK");
    return EXIT_FAILURE;
  }

  // If delivery was not successfull
  if(!deliverySuccess){
    Serial.println("[WiFi] Delivery failed");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;  
  
}

// Receive a packet from queue (returns len received)
int Wifi::receivePacket(void *address, uint8_t *buf, size_t bufLen){

  // Take out of queue
  RxFrame frame;
  if(xQueueReceive(rxQueue,&frame,0) != pdTRUE){
    return -1;
  }

  // Frame can't be transferred to buffer due to length
  if(frame.len > bufLen){
    Serial.println("[WiFi] Receive buffer too small, frame dropped");
    return -2;
  }

  // If address not specified
  if(address != nullptr){
    memcpy(address,frame.mac,WIFI_ADDRESS_LEN);
  }
  memcpy(buf,frame.data,frame.len);

  return frame.len;
  
}

