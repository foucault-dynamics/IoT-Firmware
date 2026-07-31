/*
 * NEXTGEN IEMS gateway (LilyGo / TTGO ESP32 LoRa + OLED).
 *
 * Listens for readings from the substation over LoRa, acknowledges each one so
 * the substation can retire it, and republishes the raw frame to MQTT.
 *
 * WiFi and MQTT both reconnect on their own in the background, so a broker or
 * access point outage never stalls the LoRa receive loop. Readings still get
 * acknowledged during an outage; they are simply not forwarded.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "board_lilygo.h"
#include "secrets.h"
#include "shared_payload.h"
#include "ssd1306.h"
#include "sx127x.h"

static const char *TAG = "gateway";

/* Set to 0 to run the LoRa side standalone, with no WiFi and no broker. */
#define ENABLE_MQTT 1

/*
 * The deployment network resolved broker hostnames unreliably, so the gateway
 * overrides the DHCP-supplied resolver. Set to 0 to keep the network's own DNS.
 */
#define FORCE_PUBLIC_DNS 1
#define PUBLIC_DNS_IPV4  "8.8.8.8"

/* LoRa modem settings. Must match the substation exactly. */
#define LORA_SPREADING_FACTOR 10
#define LORA_BANDWIDTH_HZ     125000
#define LORA_CODING_RATE      5      /* 4/5 */
#define LORA_PREAMBLE_LEN     8
#define LORA_SYNC_WORD        0xf3
#define LORA_TX_POWER_DBM     14

/*
 * The substation switches from transmit to receive as soon as its frame is on
 * the air. This gap gives it time to get there before the acknowledgement
 * starts, and is far inside the substation's 1500 ms ACK timeout.
 */
#define ACK_GUARD_MS 30

static sx127x_handle_t  s_radio;
static ssd1306_handle_t s_display;

#if ENABLE_MQTT
static esp_mqtt_client_handle_t s_mqtt;
static volatile bool            s_mqtt_connected;
#endif

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

#if ENABLE_MQTT

/* --- WiFi --- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "WiFi connected, IP " IPSTR, IP2STR(&event->ip_info.ip));

#if FORCE_PUBLIC_DNS
        esp_netif_dns_info_t dns = {0};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        if (esp_netif_str_to_ip4(PUBLIC_DNS_IPV4, &dns.ip.u_addr.ip4) == ESP_OK &&
            esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
            ESP_LOGI(TAG, "DNS overridden to %s", PUBLIC_DNS_IPV4);
        } else {
            ESP_LOGW(TAG, "Could not override DNS, using the network's own");
        }
#endif
    }
}

static void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_wifi_event, NULL, NULL));

    wifi_config_t wifi_cfg = {0};
    /* strncpy without a terminator is intended: these are fixed-size byte arrays. */
    strncpy((char *)wifi_cfg.sta.ssid, SECRET_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, SECRET_WIFI_PASS, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID \"%s\" in the background", SECRET_WIFI_SSID);
}

/* --- MQTT --- */

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_handle_t event = data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_connected = true;
        ESP_LOGI(TAG, "MQTT connected to %s:%d", SECRET_MQTT_SERVER, SECRET_MQTT_PORT);
        show_status("GATEWAY", "MQTT connected", "Listening LoRa");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected, the client will retry");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error, transport reported %d",
                 event->error_handle ? event->error_handle->error_type : -1);
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    const esp_mqtt_client_config_t cfg = {
        .broker.address.hostname  = SECRET_MQTT_SERVER,
        .broker.address.port      = SECRET_MQTT_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
    };

    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return;
    }
    /* esp-mqtt owns its own task and reconnects on its own. */
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, on_mqtt_event, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt));
}

#endif /* ENABLE_MQTT */

/* --- LoRa --- */

static void handle_reading(const payload_t *reading, const sx127x_rx_info_t *rx)
{
    ESP_LOGI(TAG, "LoRa RX | RSSI: %d dBm | SNR: %.1f dB", rx->rssi_dbm, rx->snr_db);
    ESP_LOGI(TAG, "  UID: %" PRIu32 " | SEQ: %" PRIu32 " | import: %.2f kWh | volt: %.1f V",
             reading->uid, reading->seq, reading->kwh_import, reading->voltage);

    /*
     * Acknowledge first, before the display and before MQTT. The substation's
     * timeout is already running, whereas a screen refresh costs about 20 ms of
     * I2C and MQTT has no deadline at all.
     */
    const ack_payload_t ack = {.uid = reading->uid, .seq = reading->seq};

    vTaskDelay(pdMS_TO_TICKS(ACK_GUARD_MS));
    esp_err_t err = sx127x_send(s_radio, &ack, sizeof(ack));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "  ACK sent for SEQ %" PRIu32, reading->seq);
    } else {
        ESP_LOGE(TAG, "  ACK send failed: %s", esp_err_to_name(err));
    }

    /* Back to listening straight away, whatever happened to the ACK. */
    err = sx127x_start_receive(s_radio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot re-enter receive: %s", esp_err_to_name(err));
    }

    char uid_line[SSD1306_COLS + 1];
    snprintf(uid_line, sizeof(uid_line), "UID: %" PRIu32, reading->uid);

    char rssi_line[SSD1306_COLS + 1];
    snprintf(rssi_line, sizeof(rssi_line), "RSSI: %d dBm", rx->rssi_dbm);
    show_status("LORA RX OK", uid_line, rssi_line);

#if ENABLE_MQTT
    if (!s_mqtt || !s_mqtt_connected) {
        ESP_LOGW(TAG, "  MQTT offline, reading not forwarded");
        show_status("DATA FWD", uid_line, "MQTT offline");
        return;
    }

    /* Publish the frame verbatim: the broker side already parses this layout. */
    int msg_id = esp_mqtt_client_publish(s_mqtt, SECRET_MQTT_TOPIC, (const char *)reading,
                                         sizeof(*reading), 0, 0);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "  Published %u bytes to %s", (unsigned)sizeof(*reading), SECRET_MQTT_TOPIC);
        show_status("DATA FWD", uid_line, "Pub: SUCCESS");
    } else {
        ESP_LOGE(TAG, "  Publish failed");
        show_status("DATA FWD", uid_line, "Pub: FAILED");
    }
#endif
}

/* --- Entry point --- */

void app_main(void)
{
    ESP_LOGI(TAG, "--- Gateway booting ---");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

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
    show_status("GATEWAY", "Booting...", "");

#if ENABLE_MQTT
    wifi_start();
    mqtt_start();
#else
    ESP_LOGW(TAG, "WiFi and MQTT are disabled in this build");
#endif

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
    ESP_ERROR_CHECK(sx127x_start_receive(s_radio));

    ESP_LOGI(TAG, "Gateway ready, listening for LoRa");
    show_status("GATEWAY", "Ready", "Listening LoRa");

    for (;;) {
        /*
         * One buffer for both frame types, sized by the larger of the two, so a
         * data frame and an acknowledgement can be told apart by length after
         * the read rather than being rejected before it.
         */
        uint8_t frame[sizeof(payload_t)];
        size_t  len = 0;
        sx127x_rx_info_t rx = {0};

        err = sx127x_poll_receive(s_radio, frame, sizeof(frame), &len, &rx);

        if (err == ESP_OK && len == sizeof(payload_t)) {
            payload_t reading;
            memcpy(&reading, frame, sizeof(reading));
            handle_reading(&reading, &rx);
        } else if (err == ESP_OK && len == sizeof(ack_payload_t)) {
            /* Another node's acknowledgement; not ours to act on. */
        } else if (err == ESP_OK || err == ESP_ERR_INVALID_SIZE) {
            ESP_LOGW(TAG, "Unexpected %u byte frame (data frames are %u bytes)",
                     (unsigned)len, (unsigned)sizeof(payload_t));
            char got_line[SSD1306_COLS + 1];
            snprintf(got_line, sizeof(got_line), "Got: %u B", (unsigned)len);
            show_status("RX ERROR", "Size mismatch", got_line);
        } else if (err == ESP_ERR_INVALID_CRC) {
            ESP_LOGW(TAG, "Dropped a frame that failed its CRC");
        } else if (err != ESP_ERR_NOT_FOUND) {
            /*
             * The radio fell out of receive, which only an SPI fault can cause.
             * Re-arm rather than spinning on a radio that will never hear
             * anything again.
             */
            ESP_LOGE(TAG, "Receive stalled (%s), re-arming", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            sx127x_standby(s_radio); /* clears the cached receive state */
            sx127x_start_receive(s_radio);
        }
        /* ESP_ERR_NOT_FOUND simply means nothing has arrived yet. */

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
