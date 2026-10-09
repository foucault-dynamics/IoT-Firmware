/**
 * @file
 * Config struct for the substation node.
 */

#ifndef SUBSTATION_CONFIG_H
#define SUBSTATION_CONFIG_H

#include <cstdint>
#include "networking_config.h"

/** Everything the substation needs, filled in by loadSubstationConfig(). */
struct SubstationConfig {
  LoRaConfig lora;        ///< Radio parameters.
  LoRaLinkConfig link;    ///< ACK and retry behaviour.
  uint8_t espNowChannel;  ///< Wi-Fi channel meter nodes send on. Must match theirs.
};

#endif
