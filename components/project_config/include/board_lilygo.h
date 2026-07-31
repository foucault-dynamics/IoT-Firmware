/*
 * Pin map for the LilyGo / TTGO ESP32 LoRa+OLED board used as both the
 * substation relay and the gateway. Values are carried over unchanged from the
 * Arduino firmware, so the wiring assumptions are identical.
 */
#ifndef BOARD_LILYGO_H
#define BOARD_LILYGO_H

/* SX1276 LoRa radio, on SPI2 (HSPI) via the GPIO matrix. */
#define BOARD_LORA_SCK   5
#define BOARD_LORA_MISO  19
#define BOARD_LORA_MOSI  27
#define BOARD_LORA_CS    18
#define BOARD_LORA_RST   14
/*
 * DIO0 (GPIO 26) is wired but deliberately unused: the driver polls the
 * radio's IRQ register instead of taking a GPIO interrupt. The Arduino
 * firmware this was translated from also never exercised DIO0 (the blocking
 * LoRa API polls registers), so leaving it out keeps the proven behaviour and
 * removes a failure mode we cannot verify without the board in hand.
 */
#define BOARD_LORA_DIO0  26

/* SSD1306 128x64 OLED, I2C address 0x3C. */
#define BOARD_OLED_SDA   21
#define BOARD_OLED_SCL   22
#define BOARD_OLED_ADDR  0x3C

#endif /* BOARD_LILYGO_H */
