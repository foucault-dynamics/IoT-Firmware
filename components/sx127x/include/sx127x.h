/*
 * Minimal SX1276/77/78/79 LoRa driver for ESP-IDF.
 *
 * Scope is deliberately narrow: half-duplex, explicit header, blocking send,
 * polled receive. That is exactly what the NEXTGEN IEMS relay and gateway need,
 * and it is the same feature set the Arduino firmware used before the port.
 *
 * Modem settings are chosen to reproduce the previous sandeepmistry/LoRa
 * configuration bit for bit, so a ported device stays on the air with the same
 * spreading factor, bandwidth, coding rate, preamble and sync word.
 *
 * Threading: a device handle is NOT thread safe. Drive it from one task.
 */
#ifndef SX127X_H
#define SX127X_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The SPI bus is initialised without DMA, which caps a single transfer at the
 * hardware FIFO size (64 bytes). One byte of every transfer is the register
 * address, leaving 63 bytes of LoRa payload. Both frames in this project are
 * far below that (26 and 8 bytes); sx127x_send() rejects anything larger
 * rather than silently truncating.
 */
#define SX127X_MAX_PAYLOAD 63

typedef struct sx127x_dev *sx127x_handle_t;

typedef struct {
    spi_host_device_t host;      /* SPI host, e.g. SPI2_HOST */
    gpio_num_t        pin_sck;
    gpio_num_t        pin_miso;
    gpio_num_t        pin_mosi;
    gpio_num_t        pin_cs;
    gpio_num_t        pin_rst;
    uint32_t          frequency_hz;   /* carrier, e.g. 433000000 */
    uint8_t           spreading_factor; /* 6..12 */
    uint32_t          bandwidth_hz;   /* e.g. 125000 */
    uint8_t           coding_rate;    /* 5..8, meaning 4/5 .. 4/8 */
    uint16_t          preamble_len;   /* symbols, e.g. 8 */
    uint8_t           sync_word;      /* e.g. 0xF3 */
    int8_t            tx_power_dbm;   /* 2..17 on the PA_BOOST pin */
    bool              enable_crc;
} sx127x_config_t;

/* Metadata for the most recently received frame. */
typedef struct {
    int   rssi_dbm;
    float snr_db;
} sx127x_rx_info_t;

/*
 * Reset the radio, verify its silicon version, apply cfg and leave it in
 * standby. Also initialises the SPI bus given in cfg->host.
 */
esp_err_t sx127x_init(const sx127x_config_t *cfg, sx127x_handle_t *out_dev);

/*
 * Transmit one frame and block until the radio reports TxDone.
 * Leaves the radio in standby; call sx127x_start_receive() to listen again.
 * Returns ESP_ERR_INVALID_SIZE if len exceeds SX127X_MAX_PAYLOAD,
 * ESP_ERR_TIMEOUT if TxDone never arrives.
 */
esp_err_t sx127x_send(sx127x_handle_t dev, const void *data, size_t len);

/* Put the radio into continuous receive. Safe to call when already receiving. */
esp_err_t sx127x_start_receive(sx127x_handle_t dev);

/*
 * Non-blocking poll for a received frame. Must be preceded by
 * sx127x_start_receive(). The radio stays in continuous receive throughout, so
 * no frame is missed between polls.
 *
 * Returns ESP_OK and sets *out_len when a CRC-valid frame was read,
 * ESP_ERR_NOT_FOUND when nothing is waiting,
 * ESP_ERR_INVALID_CRC when a corrupt frame was discarded,
 * ESP_ERR_INVALID_SIZE when a frame arrived that does not fit in buf.
 * info may be NULL.
 */
esp_err_t sx127x_poll_receive(sx127x_handle_t dev, void *buf, size_t buf_size,
                              size_t *out_len, sx127x_rx_info_t *info);

/* Drop the radio into standby (lowest state that keeps the config loaded). */
esp_err_t sx127x_standby(sx127x_handle_t dev);

#ifdef __cplusplus
}
#endif

#endif /* SX127X_H */
