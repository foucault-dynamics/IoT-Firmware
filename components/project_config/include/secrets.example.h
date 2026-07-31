/*
 * Template for components/project_config/include/secrets.h.
 * Copy to secrets.h and fill in your own values.
 */
#pragma once

/* Gateway only: the WiFi network the MQTT broker is reachable from. */
#define SECRET_WIFI_SSID "your-ssid"
#define SECRET_WIFI_PASS "your-password"

/* Gateway only: MQTT broker. Port 1883 is plain (non-TLS) MQTT. */
#define SECRET_MQTT_SERVER "broker.hivemq.com"
#define SECRET_MQTT_PORT 1883
#define SECRET_MQTT_TOPIC "your/mqtt/topic"

/*
 * Meter node only: MAC address of the substation relay. Flash the substation
 * first, read the MAC it prints on boot, then paste it here.
 */
#define SECRET_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}

/*
 * Both LoRa devices: carrier frequency in Hz. Must match on substation and
 * gateway, and must be legal in your region.
 */
#define SECRET_LORA_BAND 433000000
