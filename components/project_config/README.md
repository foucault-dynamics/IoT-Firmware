# `project_config`

Board pin map and local secrets, shared by all three firmwares, use for module wide constants.

This component contains no source files. Its `CMakeLists.txt` is a single line:

```cmake
idf_component_register(INCLUDE_DIRS "include")
```

Nothing is compiled here; the component exists purely so that every firmware
picks up the same headers on its include path without relative paths like
`../../components/...`.

## Header files

### `board_lilygo.h`

Pin map for the LilyGo / TTGO ESP32 LoRa board, which is used as both the
substation relay and the gateway. Values are carried over unchanged from the
Arduino firmware, so the wiring assumptions are identical to the version that
was known to work.

| Macro | GPIO | Purpose |
|---|---|---|
| `BOARD_LORA_SCK` | 5 | SX1276 SPI clock |
| `BOARD_LORA_MISO` | 19 | SX1276 SPI data in |
| `BOARD_LORA_MOSI` | 27 | SX1276 SPI data out |
| `BOARD_LORA_CS` | 18 | SX1276 chip select |
| `BOARD_LORA_RST` | 14 | SX1276 reset (active low) |
| `BOARD_LORA_DIO0` | 26 | Radio interrupt line, defined but unused |

`BOARD_LORA_DIO0` is defined for documentation but deliberately not used. The
`sx127x` driver polls the radio's interrupt register instead of taking a GPIO
interrupt, which is what the Arduino firmware did as well. Keeping it that way
preserves proven behaviour and avoids a failure mode that cannot be tested
without the board in hand.

The ESP32-C3 SuperMini meter node has different hardware and does not include
this header.

### `secrets.h` (local, not committed)

Deployment-specific values: WiFi credentials, MQTT broker details, the
substation's MAC address, and the LoRa carrier frequency. Copy
`secrets.example.h` to `secrets.h` and fill it in before the first build.

| Macro | Used by | Meaning |
|---|---|---|
| `SECRET_WIFI_SSID` / `SECRET_WIFI_PASS` | gateway | Network the broker is reachable from |
| `SECRET_MQTT_SERVER` | gateway | Broker address |
| `SECRET_MQTT_PORT` | gateway | Broker port; `1883` for plain MQTT |
| `SECRET_MQTT_TOPIC` | gateway | Topic meter data is published to |
| `SECRET_MAC` | meter node | MAC address of the substation, the ESP-NOW peer |
| `SECRET_LORA_BAND` | substation, gateway | Carrier frequency in Hz, e.g. `433000000` |

`SECRET_LORA_BAND` is plain Hz, not the Arduino float form (`433E6`). Both LoRa
devices must use the same value, and it must be legal in your region.

`SECRET_MAC` is only known after the substation has been flashed once: it prints
its MAC address on boot. Flash the substation first, copy the address here, then
build the meter node.

### `secrets.example.h`

Committed template for the above, with placeholder values and comments
explaining which device needs which field.

