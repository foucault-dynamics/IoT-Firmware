#include "nvs_config.h"
#include "nvs_read.h"

#include "secrets.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

namespace {

// IR optical-port pins. Board wiring, never NVS keys.
// TODO: real pins once the EE team's UART-to-IR circuit is wired up.
const uint8_t IR_RX_PIN = 4;
const uint8_t IR_TX_PIN = 5;

}  // namespace

// Load config for an IR (optical port) meter node
IrNodeConfig loadIrNodeConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  IrNodeConfig cfg{};

  // Set unique ID for board
#if CONFIG_IDF_TARGET_ESP32C3
  esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, cfg.uid, sizeof(cfg.uid) * 8);
#endif

  cfg.communityId = static_cast<uint8_t>(readU32("comm_id", 0));
  cfg.unitId = static_cast<uint8_t>(readU32("unit_id", 0));
  cfg.simulate = readU32("simulate", 1) != 0;  // default simulated: real circuit doesn't exist yet

  cfg.espNow.channel = static_cast<uint8_t>(readU32("espnow_chan", 6));  // match substation's fixed listen channel
  cfg.espNow.sendTimeoutMs = readU32("espnow_to_ms", 100);

  uint8_t defaultMac[] = SECRET_MAC;
  readMac("sub_mac", defaultMac, cfg.substation.mac);

  cfg.iec.pollIntervalMs = readU32("poll_ms", 60000);
  cfg.iec.bus.rx = IR_RX_PIN;
  cfg.iec.bus.tx = IR_TX_PIN;
  cfg.iec.bus.baudRate = 300;       // IEC 62056-21 always starts at 300 baud -- not NVS-tunable
  cfg.iec.bus.format = SERIAL_7E1;  // fixed by the standard

  prefs.end();
  return cfg;
}