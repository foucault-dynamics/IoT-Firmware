# secrets

`secrets.h` holds the firmware defaults for every node: credentials, network
addresses and radio settings. Nothing reads these macros directly at runtime.
The NVS loaders in `../nvs_config/` use them as the fallback when a key has not
been set over serial (`set <key> <value>`). See `../nvs_config/NVS_KEYS.md` for
the full key list.

`secrets.h` is tracked in git. Keep real credentials out of it and set them over
serial instead, so they live in NVS on the device.

## Settings

### WiFi (gateway)

| Macro | NVS key | Purpose |
|---|---|---|
| `SECRET_WIFI_SSID` | `wifi_ssid` | Router the gateway joins to reach the internet and the MQTT broker |
| `SECRET_WIFI_PASS` | `wifi_pass` | Password for that router |

### MQTT (gateway)

| Macro | NVS key | Purpose |
|---|---|---|
| `SECRET_MQTT_SERVER` | `mqtt_host` | Broker hostname the gateway publishes readings to |
| `SECRET_MQTT_PORT` | `mqtt_port` | Broker port (8883 is MQTT over TLS) |
| `SECRET_MQTT_TOPIC` | `mqtt_topic` | Topic that meter readings are published on |
| `SECRET_MQTT_USERNAME` | `mqtt_user` | Broker login. Empty means connect without auth |
| `SECRET_MQTT_PASSWORD` | `mqtt_pass` | Broker password. Empty means connect without auth |

### Radio and identity (all nodes)

| Macro | NVS key | Purpose |
|---|---|---|
| `SECRET_MAC` | `sub_mac` | MAC address of the substation, which the IR, CV and RS485 nodes send their ESP-NOW frames to |
| `SECRET_LORA_BAND` | `lora_band` | LoRa frequency in Hz. 433E6 or 915E6, and it must match on the substation and gateway and be legal in your region |

### CV node (ESP32-CAM meter reading)

| Macro | NVS key | Purpose |
|---|---|---|
| `SECRET_CAM_AP_SSID` | `ap_ssid` | Name of the WiFi access point the CV node creates. The camera joins it directly, so no router is needed at the meter site |
| `SECRET_CAM_AP_PASSWORD` | `ap_pass` | Password for that access point |
| `SECRET_CAM_HOST` | `cam_host` | IP of the camera. Fixed, because it is set as a static IP in the camera's own `wlan.ini` |
| `SECRET_CAM_FLOW_NAME` | `cam_flow` | Number name in the camera's `config.ini` (`main.dig1` / `main.ana1`), used to pick which reading to poll |
| `SECRET_CAM_USER` | `cam_user` | HTTP user for the AI-on-the-edge-device API. Empty if auth is off |
| `SECRET_CAM_PASS` | `cam_pass` | HTTP password for that API. Empty if auth is off |

### RS485 node (Modbus)

| Macro | NVS key | Purpose |
|---|---|---|
| `SECRET_AP_SSID` | `ap_ssid` | Name of the WiFi access point the RS485 node creates |
| `SECRET_AP_PASS` | `ap_pass` | Password for that access point. WPA2 needs at least 8 characters |
| `SECRET_MODBUS_SIM_HOST` | `tcp_host` | IP of the Modbus TCP simulator the node talks to |
| `SECRET_MODBUS_SIM_PORT` | `tcp_port` | Port of that simulator |

## Notes

- `ap_ssid` and `ap_pass` are the same NVS keys on the CV and RS485 nodes. Each
  node only reads its own default (`SECRET_CAM_AP_*` or `SECRET_AP_*`), so the two
  macro sets never collide.
- `SECRET_MAC` is the substation's address, not the node's own. If the substation
  board changes, update this default or set `sub_mac` over serial on each node.
