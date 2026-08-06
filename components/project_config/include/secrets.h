/*
 * Local deployment configuration.
 *
 * NOTE: this file is committed for testing convenience and contains real
 * credentials. Remove it from version control before production and keep only
 * secrets.example.h in the repository.
 */
#pragma once

#define SECRET_WIFI_SSID "Damian7777"
#define SECRET_WIFI_PASS "87654321"

#define SECRET_MQTT_SERVER "broker.hivemq.com"
#define SECRET_MQTT_PORT 1883
#define SECRET_MQTT_TOPIC "qut_ems_project_888/ems/ZoneA/meters"
#define SECRET_MAC {0xD4, 0xD4, 0xDA, 0x9E, 0x9C, 0xC8}
#define SECRET_LORA_BAND 433000000 /* 915000000 for AU915 */
