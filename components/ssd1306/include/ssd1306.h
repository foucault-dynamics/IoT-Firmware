/*
 * Minimal SSD1306 128x64 OLED driver for ESP-IDF, I2C only.
 *
 * Replaces Adafruit_SSD1306 + Adafruit_GFX from the Arduino firmware. Only what
 * the status screens need is implemented: a full-screen framebuffer, a 6x8
 * bitmap font, and a blocking flush.
 *
 * Threading: a device handle is NOT thread safe. Drive it from one task.
 */
#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_COLS    (SSD1306_WIDTH / 6)   /* 21 characters per line */
#define SSD1306_ROWS    (SSD1306_HEIGHT / 8)  /* 8 text lines */

typedef struct ssd1306_dev *ssd1306_handle_t;

typedef struct {
    i2c_port_num_t port;        /* e.g. I2C_NUM_0 */
    gpio_num_t     pin_sda;
    gpio_num_t     pin_scl;
    uint8_t        i2c_address; /* usually 0x3C */
} ssd1306_config_t;

/* Initialise the I2C bus and the panel, and clear the display. */
esp_err_t ssd1306_init(const ssd1306_config_t *cfg, ssd1306_handle_t *out_dev);

/* Blank the framebuffer. Nothing reaches the panel until ssd1306_flush(). */
void ssd1306_clear(ssd1306_handle_t dev);

/*
 * Draw text into the framebuffer at a character cell.
 * col is 0..SSD1306_COLS-1, row is 0..SSD1306_ROWS-1. Text past the right edge
 * is clipped; a row outside the panel is ignored.
 */
void ssd1306_draw_text(ssd1306_handle_t dev, uint8_t col, uint8_t row, const char *text);

/* printf-style variant of ssd1306_draw_text(). */
__attribute__((format(printf, 4, 5)))
void ssd1306_printf(ssd1306_handle_t dev, uint8_t col, uint8_t row, const char *fmt, ...);

/* Push the framebuffer to the panel. */
esp_err_t ssd1306_flush(ssd1306_handle_t dev);

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_H */
