#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H
#include <stdint.h>

struct SpiPinConfig {
  uint8_t SCK;
  uint8_t MISO;
  uint8_t MOSI;
  uint8_t SS;
  uint8_t RST;
  uint8_t DIO0;
};

struct UartPinConfig {
  uint8_t RX;
  uint8_t TX;
};


#endif
