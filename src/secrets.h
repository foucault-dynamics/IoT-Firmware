#pragma once

#define SECRET_WIFI_SSID "Damian7777"
#define SECRET_WIFI_PASS "87654321"

#define SECRET_MQTT_SERVER "broker.hivemq.com"
#define SECRET_MQTT_PORT 1883
#define SECRET_MQTT_TOPIC "qut_ems_project_888/ems/ZoneA/meters"
#define SECRET_MAC {0xF0, 0x24, 0xF9, 0x93, 0x04, 0x5C}
#define SECRET_LORA_BAND 433E6 // 915E6

// CV node's own WiFi AP. The ESP32-CAM (AI-on-the-edge-device) joins this
// directly, no router or internet needed at the meter site.
#define SECRET_CAM_AP_SSID "AMR_Base"
#define SECRET_CAM_AP_PASSWORD "AMR12345"

// AI-on-the-edge-device HTTP API, polled by the CV node.
// The cam holds a static IP set in its own wlan.ini, so this stays fixed.
#define SECRET_CAM_HOST "192.168.4.2"
// Matches the number name in the cam's config.ini (main.dig1 / main.ana1).
#define SECRET_CAM_FLOW_NAME "main"
#define SECRET_CAM_USER ""
#define SECRET_CAM_PASS ""

// RS485 node's own WiFi AP and the Modbus TCP simulator it talks to.
#define SECRET_AP_SSID "kaizen-rs485"
#define SECRET_AP_PASS "kaizen123"      // WPA2 minimum is 8 chars
#define SECRET_MODBUS_SIM_HOST "192.168.4.2"   // was 192.168.1.100
#define SECRET_MODBUS_SIM_PORT 5020

