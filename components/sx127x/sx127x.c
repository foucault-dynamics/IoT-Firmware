#include "sx127x.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sx127x";

/* --- Register map (SX1276 datasheet, LoRa mode) --- */
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID              0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_OCP                  0x0b
#define REG_LNA                  0x0c
#define REG_FIFO_ADDR_PTR        0x0d
#define REG_FIFO_TX_BASE_ADDR    0x0e
#define REG_FIFO_RX_BASE_ADDR    0x0f
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_PKT_SNR_VALUE        0x19
#define REG_PKT_RSSI_VALUE       0x1a
#define REG_MODEM_CONFIG_1       0x1d
#define REG_MODEM_CONFIG_2       0x1e
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MODEM_CONFIG_3       0x26
#define REG_DETECTION_OPTIMIZE   0x31
#define REG_DETECTION_THRESHOLD  0x37
#define REG_SYNC_WORD            0x39
#define REG_DIO_MAPPING_1        0x40
#define REG_VERSION              0x42
#define REG_PA_DAC               0x4d

/* REG_OP_MODE */
#define MODE_LONG_RANGE_MODE 0x80
#define MODE_SLEEP           0x00
#define MODE_STDBY           0x01
#define MODE_TX              0x03
#define MODE_RX_CONTINUOUS   0x05

/* REG_PA_CONFIG */
#define PA_BOOST 0x80

/* REG_IRQ_FLAGS */
#define IRQ_TX_DONE_MASK           0x08
#define IRQ_PAYLOAD_CRC_ERROR_MASK 0x20
#define IRQ_RX_DONE_MASK           0x40

#define SX127X_VERSION 0x12

/*
 * RSSI is reported relative to the front-end port in use. The SX1276 datasheet
 * gives -164 dBm for the low-frequency port and -157 dBm for the high-frequency
 * port, with the split at 525 MHz.
 */
#define RF_MID_BAND_THRESHOLD_HZ 525000000UL
#define RSSI_OFFSET_LF_PORT      164
#define RSSI_OFFSET_HF_PORT      157

#define SPI_CLOCK_HZ  8000000  /* SX127x tolerates up to 10 MHz */
#define TX_TIMEOUT_MS 4000     /* far above the ~0.5 s airtime at SF12/BW125 */

struct sx127x_dev {
    spi_device_handle_t spi;
    spi_host_device_t   host;
    gpio_num_t          pin_rst;
    uint32_t            frequency_hz;
    bool                receiving;
};

/* --- SPI primitives --- */

static esp_err_t reg_write(sx127x_handle_t dev, uint8_t reg, uint8_t value)
{
    spi_transaction_t t = {
        .flags     = SPI_TRANS_USE_TXDATA,
        .length    = 16,
        .tx_data   = {reg | 0x80, value, 0, 0},
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t reg_read(sx127x_handle_t dev, uint8_t reg, uint8_t *value)
{
    spi_transaction_t t = {
        .flags     = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length    = 16,
        .tx_data   = {reg & 0x7f, 0x00, 0, 0},
    };
    esp_err_t err = spi_device_polling_transmit(dev->spi, &t);
    if (err == ESP_OK) {
        *value = t.rx_data[1];
    }
    return err;
}

/*
 * The FIFO register does not auto-increment its address, so a burst of N data
 * bytes after one address byte reads or writes N consecutive FIFO locations.
 */
static esp_err_t fifo_write(sx127x_handle_t dev, const uint8_t *data, size_t len)
{
    uint8_t buf[SX127X_MAX_PAYLOAD + 1];
    buf[0] = REG_FIFO | 0x80;
    memcpy(&buf[1], data, len);

    spi_transaction_t t = {
        .length     = (len + 1) * 8,
        .tx_buffer  = buf,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t fifo_read(sx127x_handle_t dev, uint8_t *data, size_t len)
{
    uint8_t tx[SX127X_MAX_PAYLOAD + 1] = {0};
    uint8_t rx[SX127X_MAX_PAYLOAD + 1];
    tx[0] = REG_FIFO & 0x7f;

    spi_transaction_t t = {
        .length    = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_polling_transmit(dev->spi, &t);
    if (err == ESP_OK) {
        memcpy(data, &rx[1], len);
    }
    return err;
}

/* --- Modem configuration --- */

static esp_err_t set_mode(sx127x_handle_t dev, uint8_t mode)
{
    return reg_write(dev, REG_OP_MODE, MODE_LONG_RANGE_MODE | mode);
}

static esp_err_t set_frequency(sx127x_handle_t dev, uint32_t hz)
{
    /* frf = f_rf * 2^19 / f_xosc, with a 32 MHz crystal. */
    uint64_t frf = ((uint64_t)hz << 19) / 32000000ULL;
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_FRF_MSB, (uint8_t)(frf >> 16)), TAG, "frf msb");
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_FRF_MID, (uint8_t)(frf >> 8)), TAG, "frf mid");
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_FRF_LSB, (uint8_t)(frf >> 0)), TAG, "frf lsb");
    return ESP_OK;
}

static esp_err_t set_tx_power(sx127x_handle_t dev, int8_t dbm)
{
    /*
     * PA_BOOST output only, matching the previous Arduino configuration. The
     * usable range there is 2..17 dBm; the +20 dBm high-power mode is not used.
     */
    if (dbm < 2) {
        dbm = 2;
    } else if (dbm > 17) {
        dbm = 17;
    }
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_PA_DAC, 0x84), TAG, "pa dac");
    /* Overcurrent protection at 100 mA: trim = (mA - 45) / 5, OcpOn = bit 5. */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_OCP, 0x20 | (((100 - 45) / 5) & 0x1f)), TAG, "ocp");
    return reg_write(dev, REG_PA_CONFIG, PA_BOOST | (uint8_t)(dbm - 2));
}

static uint8_t bandwidth_index(uint32_t hz)
{
    static const uint32_t table[] = {
        7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000,
    };
    for (uint8_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (hz <= table[i]) {
            return i;
        }
    }
    return 9; /* 500 kHz */
}

static esp_err_t set_modem_config(sx127x_handle_t dev, const sx127x_config_t *cfg)
{
    uint8_t sf = cfg->spreading_factor;
    if (sf < 6) {
        sf = 6;
    } else if (sf > 12) {
        sf = 12;
    }
    uint8_t cr = cfg->coding_rate;
    if (cr < 5) {
        cr = 5;
    } else if (cr > 8) {
        cr = 8;
    }
    uint8_t bw = bandwidth_index(cfg->bandwidth_hz);

    /*
     * LowDataRateOptimize is mandatory once a symbol lasts longer than 16 ms,
     * to stop crystal drift smearing the symbol across the FFT bin.
     */
    uint32_t symbol_duration_ms = ((1UL << sf) * 1000UL) / cfg->bandwidth_hz;
    bool ldo = symbol_duration_ms > 16;

    /* ModemConfig1: Bw[7:4] | CodingRate[3:1] | ImplicitHeaderOn[0] = 0. */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_MODEM_CONFIG_1, (uint8_t)((bw << 4) | ((cr - 4) << 1))),
                        TAG, "modem cfg1");
    /* ModemConfig2: SF[7:4] | TxContinuous[3]=0 | RxPayloadCrcOn[2] | SymbTimeout[9:8]=0. */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_MODEM_CONFIG_2,
                                  (uint8_t)((sf << 4) | (cfg->enable_crc ? 0x04 : 0x00))),
                        TAG, "modem cfg2");
    /* ModemConfig3: LowDataRateOptimize[3] | AgcAutoOn[2]. */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_MODEM_CONFIG_3, (uint8_t)((ldo ? 0x08 : 0x00) | 0x04)),
                        TAG, "modem cfg3");

    /* SF6 is a special implicit-header-only mode with different detection settings. */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_DETECTION_OPTIMIZE, sf == 6 ? 0xc5 : 0xc3),
                        TAG, "detect opt");
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_DETECTION_THRESHOLD, sf == 6 ? 0x0c : 0x0a),
                        TAG, "detect thr");
    return ESP_OK;
}

/* --- Public API --- */

esp_err_t sx127x_init(const sx127x_config_t *cfg, sx127x_handle_t *out_dev)
{
    ESP_RETURN_ON_FALSE(cfg && out_dev, ESP_ERR_INVALID_ARG, TAG, "null arg");
    ESP_RETURN_ON_FALSE(cfg->bandwidth_hz > 0, ESP_ERR_INVALID_ARG, TAG, "bandwidth must be > 0");

    struct sx127x_dev *dev = calloc(1, sizeof(struct sx127x_dev));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "no mem");

    dev->host         = cfg->host;
    dev->pin_rst      = cfg->pin_rst;
    dev->frequency_hz = cfg->frequency_hz;

    /* ESP_GOTO_ON_* assign to a variable that must be named `ret`. */
    esp_err_t ret = ESP_OK;

    const gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << cfg->pin_rst,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&rst_cfg), fail_free, TAG, "reset gpio");

    const spi_bus_config_t bus_cfg = {
        .sclk_io_num     = cfg->pin_sck,
        .miso_io_num     = cfg->pin_miso,
        .mosi_io_num     = cfg->pin_mosi,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        /* No DMA: transfers are tiny and this removes the DMA-capable buffer
         * requirement entirely. See SX127X_MAX_PAYLOAD. */
        .max_transfer_sz = 0,
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(cfg->host, &bus_cfg, SPI_DMA_DISABLED),
                      fail_free, TAG, "spi bus init");

    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode           = 0,
        .spics_io_num   = cfg->pin_cs,
        .queue_size     = 1,
    };
    ESP_GOTO_ON_ERROR(spi_bus_add_device(cfg->host, &dev_cfg, &dev->spi),
                      fail_bus, TAG, "spi add device");

    /* Hardware reset: NRESET is active low, held well past the 100 us minimum. */
    gpio_set_level(cfg->pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(cfg->pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t version = 0;
    ESP_GOTO_ON_ERROR(reg_read(dev, REG_VERSION, &version), fail_dev, TAG, "read version");
    ESP_GOTO_ON_FALSE(version == SX127X_VERSION, ESP_ERR_NOT_FOUND, fail_dev, TAG,
                      "no SX127x found (REG_VERSION=0x%02x, expected 0x%02x); check SPI wiring",
                      version, SX127X_VERSION);

    /* LoRa mode can only be selected while the chip is asleep. */
    ESP_GOTO_ON_ERROR(set_mode(dev, MODE_SLEEP), fail_dev, TAG, "sleep");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_GOTO_ON_ERROR(set_frequency(dev, cfg->frequency_hz), fail_dev, TAG, "frequency");
    /* One FIFO, used by TX and RX in turn, so both bases sit at zero. */
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_FIFO_TX_BASE_ADDR, 0), fail_dev, TAG, "tx base");
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_FIFO_RX_BASE_ADDR, 0), fail_dev, TAG, "rx base");

    uint8_t lna = 0;
    ESP_GOTO_ON_ERROR(reg_read(dev, REG_LNA, &lna), fail_dev, TAG, "read lna");
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_LNA, lna | 0x03), fail_dev, TAG, "lna boost");

    ESP_GOTO_ON_ERROR(set_modem_config(dev, cfg), fail_dev, TAG, "modem config");
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_PREAMBLE_MSB, (uint8_t)(cfg->preamble_len >> 8)),
                      fail_dev, TAG, "preamble msb");
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_PREAMBLE_LSB, (uint8_t)(cfg->preamble_len & 0xff)),
                      fail_dev, TAG, "preamble lsb");
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_SYNC_WORD, cfg->sync_word), fail_dev, TAG, "sync word");
    ESP_GOTO_ON_ERROR(set_tx_power(dev, cfg->tx_power_dbm), fail_dev, TAG, "tx power");
    ESP_GOTO_ON_ERROR(set_mode(dev, MODE_STDBY), fail_dev, TAG, "standby");

    ESP_LOGI(TAG, "SX127x ready: %" PRIu32 " Hz, SF%d, BW %" PRIu32 " Hz, CR 4/%d, sync 0x%02x, %d dBm",
             cfg->frequency_hz, (int)cfg->spreading_factor, cfg->bandwidth_hz,
             (int)cfg->coding_rate, (unsigned)cfg->sync_word, (int)cfg->tx_power_dbm);

    *out_dev = dev;
    return ESP_OK;

fail_dev:
    spi_bus_remove_device(dev->spi);
fail_bus:
    spi_bus_free(cfg->host);
fail_free:
    free(dev);
    return ret;
}

esp_err_t sx127x_standby(sx127x_handle_t dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "null dev");
    dev->receiving = false;
    return set_mode(dev, MODE_STDBY);
}

esp_err_t sx127x_send(sx127x_handle_t dev, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(dev && data, ESP_ERR_INVALID_ARG, TAG, "null arg");
    ESP_RETURN_ON_FALSE(len > 0 && len <= SX127X_MAX_PAYLOAD, ESP_ERR_INVALID_SIZE, TAG,
                        "payload %u exceeds %d byte limit", (unsigned)len, SX127X_MAX_PAYLOAD);

    ESP_RETURN_ON_ERROR(set_mode(dev, MODE_STDBY), TAG, "standby");
    dev->receiving = false;

    /* Stale flags from a receive that was interrupted would confuse the wait below. */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_IRQ_FLAGS, 0xff), TAG, "clear irq");
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_DIO_MAPPING_1, 0x40), TAG, "dio map"); /* DIO0 = TxDone */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_FIFO_ADDR_PTR, 0), TAG, "fifo ptr");
    ESP_RETURN_ON_ERROR(fifo_write(dev, data, len), TAG, "fifo write");
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_PAYLOAD_LENGTH, (uint8_t)len), TAG, "payload len");
    ESP_RETURN_ON_ERROR(set_mode(dev, MODE_TX), TAG, "tx");

    /*
     * Poll TxDone rather than waiting on DIO0. vTaskDelay yields, so the idle
     * task and the watchdog are both serviced while the frame is on the air.
     */
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)TX_TIMEOUT_MS * 1000;
    for (;;) {
        uint8_t flags = 0;
        ESP_RETURN_ON_ERROR(reg_read(dev, REG_IRQ_FLAGS, &flags), TAG, "read irq");
        if (flags & IRQ_TX_DONE_MASK) {
            return reg_write(dev, REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);
        }
        if (esp_timer_get_time() > deadline_us) {
            ESP_LOGE(TAG, "TxDone timeout after %d ms", TX_TIMEOUT_MS);
            set_mode(dev, MODE_STDBY);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t sx127x_start_receive(sx127x_handle_t dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "null dev");
    if (dev->receiving) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_DIO_MAPPING_1, 0x00), TAG, "dio map"); /* DIO0 = RxDone */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_IRQ_FLAGS, 0xff), TAG, "clear irq");
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_FIFO_ADDR_PTR, 0), TAG, "fifo ptr");
    /*
     * Continuous rather than single receive: the modem never leaves RX, so
     * there is no re-arm window in which an inbound frame can be missed.
     */
    ESP_RETURN_ON_ERROR(set_mode(dev, MODE_RX_CONTINUOUS), TAG, "rx continuous");
    dev->receiving = true;
    return ESP_OK;
}

esp_err_t sx127x_poll_receive(sx127x_handle_t dev, void *buf, size_t buf_size,
                              size_t *out_len, sx127x_rx_info_t *info)
{
    ESP_RETURN_ON_FALSE(dev && buf && out_len, ESP_ERR_INVALID_ARG, TAG, "null arg");
    ESP_RETURN_ON_FALSE(dev->receiving, ESP_ERR_INVALID_STATE, TAG, "not receiving");

    *out_len = 0;

    uint8_t flags = 0;
    ESP_RETURN_ON_ERROR(reg_read(dev, REG_IRQ_FLAGS, &flags), TAG, "read irq");
    if (!(flags & IRQ_RX_DONE_MASK)) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Writing a flag back clears it; do this before returning on any path. */
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_IRQ_FLAGS, flags), TAG, "clear irq");

    if (flags & IRQ_PAYLOAD_CRC_ERROR_MASK) {
        return ESP_ERR_INVALID_CRC;
    }

    uint8_t len = 0;
    ESP_RETURN_ON_ERROR(reg_read(dev, REG_RX_NB_BYTES, &len), TAG, "read length");

    uint8_t rx_addr = 0;
    ESP_RETURN_ON_ERROR(reg_read(dev, REG_FIFO_RX_CURRENT_ADDR, &rx_addr), TAG, "read rx addr");
    ESP_RETURN_ON_ERROR(reg_write(dev, REG_FIFO_ADDR_PTR, rx_addr), TAG, "fifo ptr");

    if (len == 0 || len > buf_size || len > SX127X_MAX_PAYLOAD) {
        /*
         * Drain nothing and report the mismatch. The modem is still in RX, so
         * the next frame will be picked up normally.
         */
        ESP_LOGW(TAG, "dropping %u byte frame (buffer holds %u)", len, (unsigned)buf_size);
        *out_len = len;
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(fifo_read(dev, buf, len), TAG, "fifo read");

    if (info) {
        uint8_t raw_rssi = 0;
        uint8_t raw_snr  = 0;
        ESP_RETURN_ON_ERROR(reg_read(dev, REG_PKT_RSSI_VALUE, &raw_rssi), TAG, "read rssi");
        ESP_RETURN_ON_ERROR(reg_read(dev, REG_PKT_SNR_VALUE, &raw_snr), TAG, "read snr");
        int offset = (dev->frequency_hz < RF_MID_BAND_THRESHOLD_HZ) ? RSSI_OFFSET_LF_PORT
                                                                    : RSSI_OFFSET_HF_PORT;
        info->rssi_dbm = (int)raw_rssi - offset;
        info->snr_db   = (float)((int8_t)raw_snr) * 0.25f;
    }

    *out_len = len;
    return ESP_OK;
}
