/*
 * NEXTGEN IEMS meter node (ESP32-C3 SuperMini).
 *
 * Wakes on a timer, simulates one meter reading, appends it to a buffer held in
 * RTC memory, then tries to flush the whole buffer to the substation over
 * ESP-NOW. Readings that cannot be delivered stay queued for the next wake, so
 * a substation outage costs data only once the buffer overflows.
 *
 * The reading is captured *before* the radio is touched. That ordering is
 * deliberate: a radio failure must not cost us the sample.
 */
#include <inttypes.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "secrets.h"
#include "shared_payload.h"

static const char *TAG = "meter_node";

/* --- Deployment configuration --- */

/* "MINI-002" as an integer, to keep the frame small. */
#define DEVICE_UID    2
#define COMMUNITY_ID  1
#define UNIT_ID       15

#define SLEEP_TIME_SEC 30

/*
 * Neither node nor substation joins an access point, so nothing pins their
 * channel for them. Both ends set it explicitly to the same value.
 */
#define ESPNOW_CHANNEL 1

/* How long to wait for the substation's link-layer acknowledgement. */
#define SEND_ACK_TIMEOUT_MS 100

/*
 * Development mode keeps the USB serial link up by restarting instead of
 * entering deep sleep, which would drop the CDC connection. Set to 0 for
 * battery operation.
 */
#define DEV_KEEP_USB_ALIVE 1

/* --- State surviving deep sleep, in RTC memory --- */

/*
 * RTC memory survives deep sleep but is reloaded from the firmware image on any
 * boot that runs the bootloader. With DEV_KEEP_USB_ALIVE the node restarts
 * rather than sleeping, so these reset every cycle; on battery they persist.
 */
#define MAX_BUFFER_SIZE 15

static RTC_DATA_ATTR payload_t s_reading_buffer[MAX_BUFFER_SIZE];
static RTC_DATA_ATTR int       s_buffer_count;
static RTC_DATA_ATTR float     s_cumulative_import = 5000.0f;
static RTC_DATA_ATTR uint32_t  s_message_counter;

/* --- ESP-NOW send completion --- */

static SemaphoreHandle_t   s_send_done;
static esp_now_send_status_t s_send_status;

static uint8_t s_substation_mac[ESP_NOW_ETH_ALEN] = SECRET_MAC;

/*
 * Runs in the WiFi task (not an ISR), so ordinary FreeRTOS calls are allowed.
 * Note this reports the 802.11 link-layer acknowledgement from the substation's
 * radio, which proves the frame was received but not that it was relayed on.
 */
static void on_data_sent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    s_send_status = status;
    xSemaphoreGive(s_send_done);
}

/* Arduino's random(min, max) returns min..max-1; keep the same reading ranges. */
static int32_t rand_range(int32_t min, int32_t max_exclusive)
{
    return min + (int32_t)(esp_random() % (uint32_t)(max_exclusive - min));
}

/* --- Setup --- */

static esp_err_t wifi_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* Keep credentials out of NVS: nothing to persist, and it saves flash wear. */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    return esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

static esp_err_t espnow_init(void)
{
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_now_register_send_cb(on_data_sent);
    if (err != ESP_OK) {
        return err;
    }

    /*
     * Zero the whole struct before filling it in. Leaving ifidx uninitialised
     * makes esp_now_send() fail with ESP_ERR_ESPNOW_IF on a bad stack value.
     */
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_substation_mac, ESP_NOW_ETH_ALEN);
    peer.channel = 0; /* 0 means "whatever channel the interface is on" */
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer);
}

/* --- Reading generation --- */

static void capture_reading(void)
{
    s_message_counter++;
    s_cumulative_import += (float)rand_range(1, 10) / 100.0f;

    if (s_buffer_count >= MAX_BUFFER_SIZE) {
        ESP_LOGW(TAG, "Buffer full, dropping oldest reading");
        memmove(&s_reading_buffer[0], &s_reading_buffer[1],
                sizeof(payload_t) * (MAX_BUFFER_SIZE - 1));
        s_buffer_count = MAX_BUFFER_SIZE - 1;
    }

    payload_t *reading = &s_reading_buffer[s_buffer_count];
    reading->uid          = DEVICE_UID;
    reading->seq          = s_message_counter;
    reading->kwh_import   = s_cumulative_import;
    reading->kwh_export   = 0.0f; /* simulated: no export yet */
    reading->voltage      = 235.0f + (float)rand_range(-5, 6);
    reading->battery_v    = 3.7f + (float)rand_range(-1, 2) / 10.0f;
    reading->community_id = COMMUNITY_ID;
    reading->unit_id      = UNIT_ID;
    s_buffer_count++;

    ESP_LOGI(TAG, "Buffered reading %d/%d | UID: %" PRIu32 " | SEQ: %" PRIu32,
             s_buffer_count, MAX_BUFFER_SIZE, reading->uid, reading->seq);
}

/* --- Buffer flush --- */

/* Returns the number of readings acknowledged, starting from the oldest. */
static int flush_buffer(void)
{
    int sent = 0;

    for (int i = 0; i < s_buffer_count; i++) {
        /* Discard any completion left over from a send that already timed out. */
        xSemaphoreTake(s_send_done, 0);
        s_send_status = ESP_NOW_SEND_FAIL;

        esp_err_t err = esp_now_send(s_substation_mac, (const uint8_t *)&s_reading_buffer[i],
                                     sizeof(payload_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SEQ %" PRIu32 " send error: %s", s_reading_buffer[i].seq,
                     esp_err_to_name(err));
            break;
        }

        if (xSemaphoreTake(s_send_done, pdMS_TO_TICKS(SEND_ACK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "SEQ %" PRIu32 " no completion within %d ms, stopping flush",
                     s_reading_buffer[i].seq, SEND_ACK_TIMEOUT_MS);
            break;
        }
        if (s_send_status != ESP_NOW_SEND_SUCCESS) {
            ESP_LOGW(TAG, "SEQ %" PRIu32 " not acknowledged, stopping flush",
                     s_reading_buffer[i].seq);
            break; /* stop at the first gap so the queue stays in order */
        }

        ESP_LOGI(TAG, "SEQ %" PRIu32 " delivered", s_reading_buffer[i].seq);
        sent++;
    }

    return sent;
}

static void drop_sent_readings(int sent)
{
    if (sent <= 0) {
        return;
    }
    const int remaining = s_buffer_count - sent;
    if (remaining > 0) {
        memmove(&s_reading_buffer[0], &s_reading_buffer[sent], sizeof(payload_t) * remaining);
    }
    s_buffer_count = remaining;
    ESP_LOGI(TAG, "Queue flushed, %d reading(s) still buffered", s_buffer_count);
}

/* --- Power down --- */

static void enter_low_power(int64_t start_us)
{
    ESP_LOGI(TAG, "Active time: %" PRId64 " ms. Sleeping for %d s",
             (esp_timer_get_time() - start_us) / 1000, SLEEP_TIME_SEC);

    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();

#if DEV_KEEP_USB_ALIVE
    ESP_LOGI(TAG, "Dev mode: restarting instead of sleeping to keep USB alive");
    vTaskDelay(pdMS_TO_TICKS(SLEEP_TIME_SEC * 1000));
    esp_restart();
#else
    esp_deep_sleep((uint64_t)SLEEP_TIME_SEC * 1000000ULL);
#endif
}

/* --- Entry point --- */

void app_main(void)
{
    const int64_t start_us = esp_timer_get_time();

    ESP_LOGI(TAG, "--- SuperMini fault-tolerant wake ---");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /*
     * A wake that is not from the timer is a power-on or hardware reset, which
     * also means RTC memory was just reloaded from flash. Start the sequence at
     * a random point so a restarted node does not replay sequence numbers that
     * the broker's deduplication has already seen.
     */
    if (!(esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER))) {
        s_message_counter = esp_random() & 0xffffff;
        ESP_LOGI(TAG, "Cold boot, starting sequence at %" PRIu32, s_message_counter);
    }

    capture_reading();

    s_send_done = xSemaphoreCreateBinary();
    if (!s_send_done) {
        ESP_LOGE(TAG, "Cannot allocate send semaphore, reading stays buffered");
        enter_low_power(start_us);
        return;
    }

    err = wifi_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s, reading stays buffered", esp_err_to_name(err));
        enter_low_power(start_us);
        return;
    }

    uint8_t mac[ESP_NOW_ETH_ALEN] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        ESP_LOGI(TAG, "Node MAC " MACSTR " -> substation " MACSTR,
                 MAC2STR(mac), MAC2STR(s_substation_mac));
    }

    err = espnow_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s, reading stays buffered", esp_err_to_name(err));
        enter_low_power(start_us);
        return;
    }

    drop_sent_readings(flush_buffer());
    enter_low_power(start_us);
}
