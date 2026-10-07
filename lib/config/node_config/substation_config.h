#ifndef SUBSTATION_CONFIG_H
#define SUBSTATION_CONFIG_H

#include <cstdint>
#include "networking_config.h"

struct SubstationConfig {
  LoRaConfig lora;
  LoRaLinkConfig link;
  uint8_t espNowChannel;
};

#endif