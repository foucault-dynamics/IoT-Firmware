/**
 * @file
 * Config loader for the CV (camera) node: its NVS keys and defaults.
 */

#include "nvs_config.h"
#include "nvs_read.h"

#include <cstdlib>
#include <cstring>

#include "secrets.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

int loadCvNodeConfig(CvNodeConfig &cfg) {
  if (openConfigNvs() != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }

  cfg = CvNodeConfig{};

  // The UID is burned into eFuse at the factory, so it never comes from NVS.
#if CONFIG_IDF_TARGET_ESP32C3
  esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, cfg.uid, sizeof(cfg.uid) * 8);
#endif

  cfg.communityId = static_cast<uint8_t>(readU32("comm_id", 0));
  cfg.unitId = static_cast<uint8_t>(readU32("unit_id", 0));

  readStr("ap_ssid", SECRET_CAM_AP_SSID, cfg.ap.ssid, sizeof(cfg.ap.ssid));
  readStr("ap_pass", SECRET_CAM_AP_PASSWORD, cfg.ap.password, sizeof(cfg.ap.password));
  // The cam sits on a static IP at the bottom of the subnet (SECRET_CAM_HOST),
  // so DHCP starts well above it. Otherwise the first laptop or phone to join
  // gets .2 and the cam becomes unreachable.
  cfg.ap.dhcpStart = 100;

  cfg.espNow.channel = static_cast<uint8_t>(readU32("espnow_chan", 1));
  cfg.espNow.sendTimeoutMs = readU32("espnow_to_ms", 200);


  cfg.cam.pollIntervalMs = readU32("poll_ms", 30000);
  cfg.cam.bus.requestTimeoutMs = 5000;
  readStr("cam_user", SECRET_CAM_USER, cfg.cam.bus.httpUser, sizeof(cfg.cam.bus.httpUser));
  readStr("cam_pass", SECRET_CAM_PASS, cfg.cam.bus.httpPass, sizeof(cfg.cam.bus.httpPass));
  readStr("cam_host", SECRET_CAM_HOST, cfg.cam.host, sizeof(cfg.cam.host));
  strncpy(cfg.cam.path, "/json", sizeof(cfg.cam.path) - 1);
  cfg.cam.path[sizeof(cfg.cam.path) - 1] = '\0';
  readStr("cam_flow", SECRET_CAM_FLOW_NAME, cfg.cam.flowName, sizeof(cfg.cam.flowName));
  readStr("cam_flow_exp", SECRET_CAM_EXPORT_FLOW_NAME, cfg.cam.exportFlowName, sizeof(cfg.cam.exportFlowName));

  prefs.end();
  return EXIT_SUCCESS;
}
