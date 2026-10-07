/**
 * @file
 * EspNowUplink, the Transmitter meter nodes use to reach the substation.
 */

#ifndef ESP_NOW_UPLINK_H
#define ESP_NOW_UPLINK_H

#include "transmitter.h"
#include "networking_config.h"
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

/** Received frames held until receivePacket() takes them. Extra frames are dropped. */
#define ESPNOW_RX_QUEUE_DEPTH 4
/** Length of an ESP-NOW peer address (a MAC), in bytes. */
#define ESP_NOW_ADDRESS_LEN ESP_NOW_ETH_ALEN

/**
 * ESP-NOW link between meter nodes and the substation.
 *
 * Received frames are copied inside the ESP-NOW callback into a FreeRTOS
 * queue, so receivePacket() never runs in interrupt context. sendPacket()
 * blocks on a semaphore that the send callback gives, so it only returns once
 * the radio has confirmed delivery or the timeout expires.
 *
 * The ESP-NOW C API takes plain function callbacks with no user pointer, so
 * the class keeps a static #instance that the callbacks go through. That means
 * one EspNowUplink per firmware, which is fine since there is one radio.
 *
 * The address for sendPacket() and receivePacket() is a 6 byte MAC.
 */
class EspNowUplink : public Transmitter {
 private:
  /** One received frame, as queued by onReceived(). */
  struct RxFrame {
    uint8_t mac[ESP_NOW_ETH_ALEN];       ///< Sender's MAC.
    uint8_t data[ESP_NOW_MAX_DATA_LEN];  ///< Frame bytes.
    uint8_t len;                         ///< Number of valid bytes in #data.
  };

  EspNowConfig config;             ///< Channel and send timeout.
  QueueHandle_t rxQueue;           ///< Frames from onReceived() waiting for receivePacket().
  SemaphoreHandle_t sendDone;      ///< Given by onSent() when the radio reports a result.
  volatile bool deliverySuccess;   ///< Result of the last send, set by onSent().

  static EspNowUplink *instance;  ///< The one initialised uplink, used by the callbacks.

  /**
   * ESP-NOW send callback. Records the result and wakes sendPacket().
   *
   * @param[in] mac     Peer the frame was sent to.
   * @param[in] status  Whether the peer acknowledged the frame.
   */
  static void onSent(const uint8_t *mac, esp_now_send_status_t status);

  /**
   * ESP-NOW receive callback. Copies the frame into #rxQueue.
   *
   * Runs on the Wi-Fi task, so it only copies and never blocks. A frame is
   * dropped if the queue is full.
   *
   * @param[in] mac   Sender's MAC.
   * @param[in] data  Frame bytes.
   * @param[in] len   Number of bytes in @p data.
   */
  static void onReceived(const uint8_t *mac, const uint8_t *data, int len);

 public:
  /**
   * Stores the config. The radio is not touched until init().
   *
   * @param[in] config  Channel and send timeout.
   */
  EspNowUplink(EspNowConfig config);

  /**
   * Starts the radio's station interface, then ESP-NOW and its callbacks.
   *
   * @retval EXIT_SUCCESS  Ready to send and receive.
   * @retval EXIT_FAILURE  The station, queue, semaphore or ESP-NOW failed to
   *                       start.
   */
  int init() override;

  /**
   * Registers a peer so packets can be sent to it.
   *
   * The peer follows the radio's current channel and is always on the station
   * interface, even when a softAP runs beside it.
   *
   * @param[in] peer  Peer's MAC.
   * @retval EXIT_SUCCESS  Registered.
   * @retval EXIT_FAILURE  ESP-NOW refused the peer.
   */
  int addPeer(EspNowPeerConfig peer);

  /**
   * Sends one frame and waits for the peer to acknowledge it.
   *
   * @param[in] address  Peer's 6 byte MAC, registered with addPeer().
   * @param[in] buf      Frame bytes.
   * @param[in] len      1 to ESP_NOW_MAX_DATA_LEN bytes.
   * @retval EXIT_SUCCESS  The peer acknowledged the frame.
   * @retval EXIT_FAILURE  Bad length, send error, no result within
   *                       EspNowConfig::sendTimeoutMs, or delivery failed.
   */
  int sendPacket(const void *address, const uint8_t *buf, size_t len) override;

  int receivePacket(void *address, uint8_t *buf, size_t bufLen) override;
};

#endif
