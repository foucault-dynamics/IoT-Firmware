/**
 * @file
 * EspNowUplink implementation.
 */

#include "esp_now_uplink.h"
#include "esp32-hal.h"
#include "esp_now.h"
#include "esp_wifi_types.h"
#include "freertos/projdefs.h"
#include "networking_config.h"
#include "wifi_radio.h"
#include <WiFi.h>
#include <cstddef>
#include <cstdint>
#include <esp_wifi.h>
#include <cstring>
#include <cstdlib>

EspNowUplink *EspNowUplink::instance = nullptr;

EspNowUplink::EspNowUplink(EspNowConfig config){
  this->config = config;
}

void EspNowUplink::onSent(const uint8_t *mac, esp_now_send_status_t status){
  if(instance == nullptr){
    return;
  }
  instance->deliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
  Serial.printf("[EspNowUplink] Sent to %02X:%02X:%02X:%02X:%02X:%02X, status: %s\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                instance->deliverySuccess ? "SUCCESS" : "FAIL");
  xSemaphoreGive(instance->sendDone);
}

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
  // Zero wait: blocking inside the Wi-Fi task would stall the radio
  xQueueSend(instance->rxQueue,&frame,0);
}

int EspNowUplink::init(){

  // ESP-NOW rides the radio's station interface (wifi_radio.h).
  if(!wifiRadioStartStation(config.channel)){
    Serial.println("[EspNowUplink] Radio station failed to start");
    return EXIT_FAILURE;
  }

  rxQueue = xQueueCreate(ESPNOW_RX_QUEUE_DEPTH,sizeof(RxFrame));
  if(rxQueue == nullptr){
    Serial.println("[EspNowUplink] Failed to create RX queue");
    return EXIT_FAILURE;
  }

  sendDone = xSemaphoreCreateBinary();
  if(sendDone == nullptr){
    Serial.println("[EspNowUplink] Failed to create send semaphore");
    return EXIT_FAILURE;
  }

  if(esp_now_init() != ESP_OK){
    Serial.println("[EspNowUplink] Failed to init ESP-NOW");
    return EXIT_FAILURE;
  }

  // Set before registering so the callbacks never see a null instance
  instance = this;

  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceived);

  return EXIT_SUCCESS;
}


int EspNowUplink::addPeer(EspNowPeerConfig peer){

  esp_now_peer_info_t info{};
  memcpy(info.peer_addr,peer.mac,ESP_NOW_ADDRESS_LEN);
  // 0 means the peer follows the radio's current channel.
  info.channel = 0;

  // Always the station, the softAP (if any) sits beside it.
  info.ifidx = WIFI_IF_STA;

  if(esp_now_add_peer(&info) != ESP_OK){
    Serial.println("[EspNowUplink] Failed to add peer");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;

}

int EspNowUplink::sendPacket(const void *address, const uint8_t *buf, size_t len){
  if(len == 0 || len > ESP_NOW_MAX_DATA_LEN){
    Serial.println("[EspNowUplink] Invalid packet length");
    return EXIT_FAILURE;
  }

  // Clear a give left over from an earlier send that timed out
  xSemaphoreTake(sendDone,0);

  const uint8_t *mac = static_cast<const uint8_t *>(address);
  esp_err_t err = esp_now_send(mac, buf, len);
  if(err != ESP_OK){
    Serial.printf("[EspNowUplink] esp_now_send failed: %s (0x%X)\n",
                  esp_err_to_name(err), err);
    return EXIT_FAILURE;
  }

  // Blocks until onSent() reports a result or the timeout expires
  if(xSemaphoreTake(sendDone,pdMS_TO_TICKS(config.sendTimeoutMs)) != pdTRUE){
    Serial.println("[EspNowUplink] Send timeout waiting for delivery ACK");
    return EXIT_FAILURE;
  }

  if(!deliverySuccess){
    Serial.println("[EspNowUplink] Delivery failed");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;

}

int EspNowUplink::receivePacket(void *address, uint8_t *buf, size_t bufLen){

  RxFrame frame;
  if(xQueueReceive(rxQueue,&frame,0) != pdTRUE){
    return -1;
  }

  if(frame.len > bufLen){
    Serial.println("[EspNowUplink] Receive buffer too small, frame dropped");
    return -2;
  }

  // The caller passes nullptr when it does not need the sender's MAC
  if(address != nullptr){
    memcpy(address,frame.mac,ESP_NOW_ADDRESS_LEN);
  }
  memcpy(buf,frame.data,frame.len);

  return frame.len;

}

