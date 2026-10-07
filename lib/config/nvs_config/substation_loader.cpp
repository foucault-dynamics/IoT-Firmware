/**
 * @file
 * Config loader for the substation node: its NVS keys and defaults.
 */

#include "nvs_config.h"
#include "nvs_read.h"

SubstationConfig loadSubstationConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  SubstationConfig cfg{};
  cfg.lora = loadLoRaConfig();
  cfg.link = loadLoRaLinkConfig();
  cfg.espNowChannel = static_cast<uint8_t>(readU32("espnow_chan", 6));

  prefs.end();
  return cfg;
}
