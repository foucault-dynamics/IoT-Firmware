/**
 * @file
 * Config loader for the gateway node: its NVS keys and defaults.
 */

#include "nvs_config.h"
#include "nvs_read.h"

#include "secrets.h"

GatewayConfig loadGatewayConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  GatewayConfig cfg{};
  cfg.lora = loadLoRaConfig();
  cfg.link = loadLoRaLinkConfig();

  readStr("wifi_ssid", SECRET_WIFI_SSID, cfg.wifi.ssid, sizeof(cfg.wifi.ssid));
  readStr("wifi_pass", SECRET_WIFI_PASS, cfg.wifi.password, sizeof(cfg.wifi.password));

  cfg.mqtt.enabled = readU32("mqtt_on", 1) != 0;
  readStr("mqtt_host", SECRET_MQTT_SERVER, cfg.mqtt.server, sizeof(cfg.mqtt.server));
  cfg.mqtt.port = static_cast<uint16_t>(readU32("mqtt_port", SECRET_MQTT_PORT));
  readStr("mqtt_topic", SECRET_MQTT_TOPIC, cfg.mqtt.topic, sizeof(cfg.mqtt.topic));
  readStr("mqtt_user", SECRET_MQTT_USERNAME, cfg.mqtt.username, sizeof(cfg.mqtt.username));
  readStr("mqtt_pass", SECRET_MQTT_PASSWORD, cfg.mqtt.password, sizeof(cfg.mqtt.password));

  prefs.end();
  return cfg;
}
