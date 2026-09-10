#ifndef WIFI_TRANSMITTER_H
#define WIFI_TRANSMITTER_H

#include "transmitter.h"
#include "node_config.h"
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define ESPNOW_RX_QUEUE_DEPTH 4
#define WIFI_ADDRESS_LEN ESP_NOW_ETH_ALEN

class Wifi : public Transmitter {
 private:
  struct RxFrame {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
    uint8_t len;
  };

  EspNowConfig config;
  QueueHandle_t rxQueue;
  SemaphoreHandle_t sendDone;
  volatile bool deliverySuccess;

  static Wifi *instance;
  static void onSent(const uint8_t *mac, esp_now_send_status_t status);
  static void onReceived(const uint8_t *mac, const uint8_t *data, int len);

 public:
  Wifi(EspNowConfig config);
  int init() override;
  int addPeer(EspNowPeerConfig peer);
  int sendPacket(const void *address, const uint8_t *buf, size_t len) override;
  int receivePacket(void *address, uint8_t *buf, size_t bufLen) override;
};

#endif
