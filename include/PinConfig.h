#include <cstdint>
#include <stdint.h>


struct SpiPinConfig{
  uint8_t SCK;
  uint8_t MISO;
  uint8_t MOSI;
  uint8_t SS;
  uint8_t RST;
  uint8_t DIO0;  
} typedef SpiPinConfig_t;

struct UARTPinConfig{
  uint8_t RX;
  uint8_t TX;
  uint8_t DERE;
} typedef UARTPinConfig_t;
