/*
 * NEXTGEN IEMS substation relay (LilyGo / TTGO ESP32 LoRa + OLED).
 *
 * Receives readings from meter nodes over ESP-NOW and forwards each one over
 * LoRa to the gateway, retrying until the gateway acknowledges it.
 *
 * The ESP-NOW callback runs in the WiFi task and must return quickly, while a
 * LoRa relay with retries takes seconds. The two are decoupled by a queue, so
 * readings arriving mid-retry are held rather than overwritten.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "board_lilygo.h"
#include "secrets.h"
#include "shared_payload.h"
#include "ssd1306.h"
#include "sx127x.h"

static const char *TAG = "substation";

/* Must match the meter node, since neither end joins an access point. */
#define ESPNOW_CHANNEL 1

/* LoRa modem settings. Every device on this link must agree on all of them. */
#define LORA_SPREADING_FACTOR 10
#define LORA_BANDWIDTH_HZ     125000
#define LORA_CODING_RATE      5      /* 4/5 */
#define LORA_PREAMBLE_LEN     8
#define LORA_SYNC_WORD        0xf3
#define LORA_TX_POWER_DBM     14

#define LORA_MAX_RETRIES  3
#define LORA_ACK_TIMEOUT_MS 1500

/* Deep enough to ride out a burst of nodes reporting while a retry is running. */
#define RELAY_QUEUE_LEN 16

_Static_assert(sizeof(payload_t) <= SX127X_MAX_PAYLOAD,
               "payload_t does not fit in one LoRa frame");

static QueueHandle_t     s_relay_queue;
static sx127x_handle_t   s_radio;
static ssd1306_handle_t  s_display;

/* --- Display --- */

static void show_status(const char *title, const char *line1, const char *line2)
{
    if (!s_display) {
        return;
    }
    ssd1306_clear(s_display);
    ssd1306_printf(s_display, 0, 0, "=== %s ===", title);
    ssd1306_draw_text(s_display, 0, 2, line1);
    ssd1306_draw_text(s_display, 0, 3, line2);
    ssd1306_flush(s_display);
}

/* --- ESP-NOW --- */

/*
 * Runs in the WiFi task. Copies the frame onto the queue and returns; all the
 * slow work happens in the relay task.
 */
static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !info->src_addr || !data) {
        return;
    }
    if (len != sizeof(payload_t)) {
        ESP_LOGW(TAG, "Ignoring %d byte frame from " MACSTR " (expected %u)",
                 len, MAC2STR(info->src_addr), (unsigned)sizeof(payload_t));
        return;
    }

    payload_t reading;
    memcpy(&reading, data, sizeof(reading));

    if (xQueueSend(s_relay_queue, &reading, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Relay queue full, dropping SEQ %" PRIu32, reading.seq);
    }
}

static esp_err_t espnow_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE),
                        TAG, "wifi channel");

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "espnow init");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_data_recv), TAG, "espnow recv cb");
    return ESP_OK;
}

/* --- LoRa relay --- */

/* Send one reading and wait for the gateway to acknowledge it by UID and SEQ. */
static bool relay_with_ack(const payload_t *reading)
{
    for (int attempt = 1; attempt <= LORA_MAX_RETRIES; attempt++) {
        ESP_LOGI(TAG, "LoRa TX attempt %d/%d | UID: %" PRIu32 " | SEQ: %" PRIu32,
                 attempt, LORA_MAX_RETRIES, reading->uid, reading->seq);

        esp_err_t err = sx127x_send(s_radio, reading, sizeof(*reading));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LoRa send failed: %s", esp_err_to_name(err));
            continue;
        }

        /* Listen for the acknowledgement immediately: it follows within ~1 s. */
        err = sx127x_start_receive(s_radio);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot enter receive: %s", esp_err_to_name(err));
            continue;
        }

        const int64_t deadline_us = esp_timer_get_time() + (int64_t)LORA_ACK_TIMEOUT_MS * 1000;
        while (esp_timer_get_time() < deadline_us) {
            ack_payload_t ack;
            size_t        len = 0;

            err = sx127x_poll_receive(s_radio, &ack, sizeof(ack), &len, NULL);
            if (err == ESP_OK && len == sizeof(ack) &&
                ack.uid == reading->uid && ack.seq == reading->seq) {
                ESP_LOGI(TAG, "ACK received for SEQ %" PRIu32, reading->seq);
                return true;
            }
            /* Anything else (nothing waiting, CRC error, another node's ACK,
             * a data frame from a third party) is ignored; keep listening. */
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        ESP_LOGW(TAG, "ACK timeout after %d ms", LORA_ACK_TIMEOUT_MS);
        if (attempt < LORA_MAX_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(500)); /* back off so retries do not collide */
        }
    }

    ESP_LOGE(TAG, "Max retries reached for SEQ %" PRIu32 ", reading dropped", reading->seq);
    return false;
}

/* --- Entry point --- */

void app_main(void)
{
    ESP_LOGI(TAG, "--- Substation relay booting ---");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_relay_queue = xQueueCreate(RELAY_QUEUE_LEN, sizeof(payload_t));
    ESP_ERROR_CHECK(s_relay_queue ? ESP_OK : ESP_ERR_NO_MEM);

    /* The display is a convenience, so a missing panel must not stop the relay. */
    const ssd1306_config_t oled_cfg = {
        .port        = I2C_NUM_0,
        .pin_sda     = BOARD_OLED_SDA,
        .pin_scl     = BOARD_OLED_SCL,
        .i2c_address = BOARD_OLED_ADDR,
    };
    if (ssd1306_init(&oled_cfg, &s_display) != ESP_OK) {
        ESP_LOGW(TAG, "No OLED found, continuing without a display");
        s_display = NULL;
    }
    show_status("SUBSTATION", "Booting...", "");

    const sx127x_config_t lora_cfg = {
        .host             = SPI2_HOST,
        .pin_sck          = BOARD_LORA_SCK,
        .pin_miso         = BOARD_LORA_MISO,
        .pin_mosi         = BOARD_LORA_MOSI,
        .pin_cs           = BOARD_LORA_CS,
        .pin_rst          = BOARD_LORA_RST,
        .frequency_hz     = SECRET_LORA_BAND,
        .spreading_factor = LORA_SPREADING_FACTOR,
        .bandwidth_hz     = LORA_BANDWIDTH_HZ,
        .coding_rate      = LORA_CODING_RATE,
        .preamble_len     = LORA_PREAMBLE_LEN,
        .sync_word        = LORA_SYNC_WORD,
        .tx_power_dbm     = LORA_TX_POWER_DBM,
        .enable_crc       = true,
    };
    err = sx127x_init(&lora_cfg, &s_radio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LoRa init failed: %s", esp_err_to_name(err));
        show_status("ERROR", "LoRa init failed", "Check wiring");
        return; /* app_main may return; FreeRTOS keeps running */
    }

    ESP_ERROR_CHECK(espnow_init());

    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    ESP_LOGI(TAG, ">>> MAC address: " MACSTR " <<<", MAC2STR(mac));
    ESP_LOGI(TAG, "Put this MAC in SECRET_MAC on the meter node.");

    char mac_line[SSD1306_COLS + 1];
    snprintf(mac_line, sizeof(mac_line), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    show_status("SUBSTATION", "Ready to relay", mac_line);

    for (;;) {
        payload_t reading;
        if (xQueueReceive(s_relay_queue, &reading, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        char uid_line[SSD1306_COLS + 1];
        snprintf(uid_line, sizeof(uid_line), "UID: %" PRIu32, reading.uid);

        char seq_line[SSD1306_COLS + 1];
        snprintf(seq_line, sizeof(seq_line), "SEQ: %" PRIu32, reading.seq);
        show_status("RELAYING", uid_line, seq_line);

        if (relay_with_ack(&reading)) {
            show_status("RELAY OK", uid_line, "ACK received");
        } else {
            show_status("RELAY FAILED", uid_line, "Timeout, dropped");
        }
    }
}
