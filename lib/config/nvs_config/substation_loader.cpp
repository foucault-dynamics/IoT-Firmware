/**
 * @file
 * Config loader for the substation node: its NVS keys and defaults.
 */

#include "nvs_config.h"
#include "nvs_read.h"

#include <cstdlib>

int loadSubstationConfig(SubstationConfig &cfg) {
  if (openConfigNvs() != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }

  cfg = SubstationConfig{};
  cfg.lora = loadLoRaConfig();
  cfg.link = loadLoRaLinkConfig();
  cfg.espNowChannel = static_cast<uint8_t>(readU32("espnow_chan", 6));

  prefs.end();
  return EXIT_SUCCESS;
}
