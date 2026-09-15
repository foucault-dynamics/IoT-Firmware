# NVS config keys

Keys stored in the `"config"` NVS namespace (`nvs_config.cpp`), settable at
runtime over serial with `set <key> <value>` (see `nvsConfigPollSerial`).
Unset keys fall back to the default shown below (usually from `secrets.h`).

There is no enum or central table enforcing these in code — this file is the
source of truth. If a loader in `nvs_config.cpp` changes, update this file in
the same commit.

Max key length: 15 characters (`NVS_MAX_KEY_LEN`).

Value type sent over serial:
- `set key 123` → stored as uint (`putUInt`)
- `set key "text"` → stored as string (`putString`)

It must match how the key is read back (`readU32` vs `readStr`/`readMac`),
or the value will be written but silently unused / misread.

## Shared (LoRa: substation + gateway)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `lora_band` | uint | `SECRET_LORA_BAND` | Radio frequency band |
| `lora_sf` | uint | `10` | Spreading factor |
| `lora_bw` | uint | `125000` | Bandwidth (Hz) |
| `lora_sync` | uint | `0xF3` | Sync word |
| `lora_txpwr` | uint | `14` | TX power |
| `lora_retries` | uint | `3` | Max link-layer retries |
| `lora_ack_ms` | uint | `1500` | ACK timeout (ms) |

## Rs485 node (`loadRs485NodeConfig`)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `reader` | uint | `ReaderType::ModbusTCP` | Reader backend |
| `ap_ssid` | string | `SECRET_AP_SSID` | ESP-NOW AP SSID |
| `ap_pass` | string | `SECRET_AP_PASS` | ESP-NOW AP password |
| `ap_channel` | uint | `6` | AP channel |
| `espnow_to_ms` | uint | `100` | ESP-NOW send timeout (ms) |
| `sub_mac` | string (MAC) | `SECRET_MAC` | Substation MAC, `aa:bb:cc:dd:ee:ff` |
| `meter_model` | uint | `MeterModel::Simulated_Tcp` | Meter model (selects registers) |
| `poll_ms` | uint | `1000` | Meter poll interval (ms) |
| `baud` | uint | `9600` | RS485 baud rate |
| `slave_addr` | uint | `1` | Modbus slave address |
| `tcp_host` | string | `SECRET_MODBUS_SIM_HOST` | Modbus TCP simulator host |
| `tcp_port` | uint | `SECRET_MODBUS_SIM_PORT` | Modbus TCP simulator port |

## CV (camera) node (`loadCvNodeConfig`)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `uid` | uint | `3` | Node UID |
| `ap_ssid` | string | `SECRET_CAM_AP_SSID` | ESP-NOW AP SSID |
| `ap_pass` | string | `SECRET_CAM_AP_PASSWORD` | ESP-NOW AP password |
| `ap_channel` | uint | `1` | AP channel |
| `espnow_to_ms` | uint | `200` | ESP-NOW send timeout (ms) |
| `sub_mac` | string (MAC) | `SECRET_MAC` | Substation MAC |
| `poll_ms` | uint | `30000` | Camera poll interval (ms) |
| `cam_user` | string | `SECRET_CAM_USER` | Camera HTTP basic auth user |
| `cam_pass` | string | `SECRET_CAM_PASS` | Camera HTTP basic auth password |
| `cam_host` | string | `SECRET_CAM_HOST` | Camera host |
| `cam_flow` | string | `SECRET_CAM_FLOW_NAME` | Camera flow name |

Note: `ap_ssid`, `ap_pass`, `ap_channel`, `espnow_to_ms`, `sub_mac`, and
`poll_ms` are reused key names between the Rs485 and CV loaders, each with
their own defaults. This is fine as long as a given board only ever runs one
node type's firmware image against its NVS namespace.

## Gateway node (`loadGatewayConfig`)

Also loads the shared LoRa keys above, plus:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `wifi_ssid` | string | `SECRET_WIFI_SSID` | WiFi SSID |
| `wifi_pass` | string | `SECRET_WIFI_PASS` | WiFi password |
| `mqtt_on` | uint (bool) | `1` | MQTT enabled (`0`/`1`) |
| `mqtt_host` | string | `SECRET_MQTT_SERVER` | MQTT broker host |
| `mqtt_port` | uint | `SECRET_MQTT_PORT` | MQTT broker port |
| `mqtt_topic` | string | `SECRET_MQTT_TOPIC` | MQTT topic |
| `mqtt_user` | string | `SECRET_MQTT_USERNAME` | MQTT username |
| `mqtt_pass` | string | `SECRET_MQTT_PASSWORD` | MQTT password |

## Substation node (`loadSubstationConfig`)

Only the shared LoRa keys above.

## Serial commands

| Command | Effect |
|---|---|
| `set <key> <value>` | Write a key (uint, or `"quoted string"`) |
| `clear` | Wipe the whole `"config"` namespace |
| `reboot` | Restart the device (needed to apply changes) |
