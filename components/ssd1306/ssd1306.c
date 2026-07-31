#include "ssd1306.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "ssd1306_font.h"

static const char *TAG = "ssd1306";

/* I2C control byte: bit 6 selects the data register over the command register. */
#define CTRL_CMD  0x00
#define CTRL_DATA 0x40

#define I2C_SPEED_HZ   400000
#define I2C_TIMEOUT_MS 100

#define PAGES     (SSD1306_HEIGHT / 8)
#define FB_SIZE   (SSD1306_WIDTH * PAGES)
#define CELL_W    (SSD1306_FONT_WIDTH + 1) /* glyph plus the gap column */

struct ssd1306_dev {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    uint8_t framebuffer[FB_SIZE];
};

static esp_err_t send_commands(ssd1306_handle_t dev, const uint8_t *cmds, size_t len)
{
    /* Up to 8 commands per burst is plenty for every sequence used here. */
    uint8_t buf[1 + 8];
    if (len > sizeof(buf) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = CTRL_CMD;
    memcpy(&buf[1], cmds, len);
    return i2c_master_transmit(dev->dev, buf, len + 1, I2C_TIMEOUT_MS);
}

esp_err_t ssd1306_init(const ssd1306_config_t *cfg, ssd1306_handle_t *out_dev)
{
    ESP_RETURN_ON_FALSE(cfg && out_dev, ESP_ERR_INVALID_ARG, TAG, "null arg");

    struct ssd1306_dev *dev = calloc(1, sizeof(struct ssd1306_dev));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "no mem");

    /* ESP_GOTO_ON_* assign to a variable that must be named `ret`. */
    esp_err_t ret = ESP_OK;

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = cfg->port,
        .sda_io_num        = cfg->pin_sda,
        .scl_io_num        = cfg->pin_scl,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            /* The LilyGo board has external pull-ups; the internal ones are a
             * harmless backstop if a bare panel is wired up instead. */
            .enable_internal_pullup = true,
        },
    };
    ESP_GOTO_ON_ERROR(i2c_new_master_bus(&bus_cfg, &dev->bus), fail_free, TAG, "i2c bus");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = cfg->i2c_address,
        .scl_speed_hz    = I2C_SPEED_HZ,
    };
    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(dev->bus, &dev_cfg, &dev->dev),
                      fail_bus, TAG, "i2c device");

    /*
     * Panel bring-up for a 128x64 module driven from the internal charge pump.
     * Split into bursts so no single transfer exceeds the command buffer.
     */
    static const uint8_t init_a[] = {
        0xae,       /* display off */
        0xd5, 0x80, /* clock divide ratio / oscillator frequency */
        0xa8, 0x3f, /* multiplex ratio = 64 */
        0xd3, 0x00, /* display offset = 0 */
        0x40,       /* start line = 0 */
    };
    static const uint8_t init_b[] = {
        0x8d, 0x14, /* charge pump on (required when running off VCC) */
        0x20, 0x00, /* horizontal addressing mode */
        0xa1,       /* segment remap: column 127 maps to SEG0 */
        0xc8,       /* COM scan direction: remapped */
        0xda, 0x12, /* COM pins: alternative, no left/right remap */
    };
    static const uint8_t init_c[] = {
        0x81, 0xcf, /* contrast */
        0xd9, 0xf1, /* pre-charge period */
        0xdb, 0x40, /* VCOMH deselect level */
        0xa4,       /* resume from RAM content */
        0xa6,       /* normal (non-inverted) display */
    };
    static const uint8_t init_d[] = {
        0x2e, /* deactivate scroll */
        0xaf, /* display on */
    };

    ESP_GOTO_ON_ERROR(send_commands(dev, init_a, sizeof(init_a)), fail_dev, TAG, "init a");
    ESP_GOTO_ON_ERROR(send_commands(dev, init_b, sizeof(init_b)), fail_dev, TAG, "init b");
    ESP_GOTO_ON_ERROR(send_commands(dev, init_c, sizeof(init_c)), fail_dev, TAG, "init c");
    ESP_GOTO_ON_ERROR(send_commands(dev, init_d, sizeof(init_d)), fail_dev, TAG, "init d");

    ssd1306_clear(dev);
    ESP_GOTO_ON_ERROR(ssd1306_flush(dev), fail_dev, TAG, "initial flush");

    ESP_LOGI(TAG, "SSD1306 %dx%d ready at 0x%02x", SSD1306_WIDTH, SSD1306_HEIGHT,
             (unsigned)cfg->i2c_address);

    *out_dev = dev;
    return ESP_OK;

fail_dev:
    i2c_master_bus_rm_device(dev->dev);
fail_bus:
    i2c_del_master_bus(dev->bus);
fail_free:
    free(dev);
    return ret;
}

void ssd1306_clear(ssd1306_handle_t dev)
{
    if (dev) {
        memset(dev->framebuffer, 0, FB_SIZE);
    }
}

void ssd1306_draw_text(ssd1306_handle_t dev, uint8_t col, uint8_t row, const char *text)
{
    if (!dev || !text || row >= PAGES) {
        return;
    }

    uint8_t *page = &dev->framebuffer[(size_t)row * SSD1306_WIDTH];

    for (const char *p = text; *p; p++, col++) {
        if (col >= SSD1306_COLS) {
            return; /* clipped at the right edge */
        }

        unsigned char c = (unsigned char)*p;
        if (c < SSD1306_FONT_FIRST || c > SSD1306_FONT_LAST) {
            c = '?'; /* anything outside the table, including UTF-8 bytes */
        }
        const uint8_t *glyph = ssd1306_font5x7[c - SSD1306_FONT_FIRST];

        uint8_t *cell = &page[(size_t)col * CELL_W];
        memcpy(cell, glyph, SSD1306_FONT_WIDTH);
        cell[SSD1306_FONT_WIDTH] = 0x00; /* inter-character gap */
    }
}

void ssd1306_printf(ssd1306_handle_t dev, uint8_t col, uint8_t row, const char *fmt, ...)
{
    char line[SSD1306_COLS + 1];

    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    ssd1306_draw_text(dev, col, row, line);
}

esp_err_t ssd1306_flush(ssd1306_handle_t dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "null dev");

    const uint8_t window[] = {
        0x21, 0x00, SSD1306_WIDTH - 1, /* column address range */
        0x22, 0x00, PAGES - 1,         /* page address range */
    };
    ESP_RETURN_ON_ERROR(send_commands(dev, window, sizeof(window)), TAG, "set window");

    /*
     * One transfer per page. In horizontal addressing mode the panel's write
     * pointer carries over between transfers, so the pages land contiguously.
     */
    uint8_t chunk[1 + SSD1306_WIDTH];
    chunk[0] = CTRL_DATA;
    for (int page = 0; page < PAGES; page++) {
        memcpy(&chunk[1], &dev->framebuffer[(size_t)page * SSD1306_WIDTH], SSD1306_WIDTH);
        ESP_RETURN_ON_ERROR(i2c_master_transmit(dev->dev, chunk, sizeof(chunk), I2C_TIMEOUT_MS),
                            TAG, "write page %d", page);
    }
    return ESP_OK;
}
