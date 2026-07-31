# Project Kaizen

Project Kaizen  is an ESP-IDF prototype for a fault-tolerant intelligent energy metering system. It simulates energy meter readings on an ESP32-C3 SuperMini, sends the readings to a LilyGo substation using ESP-NOW, relays the payload over LoRa, and forwards the received data to an MQTT broker from the gateway.

Built with [PlatformIO](https://platformio.org/), which supplies the toolchain and **ESP-IDF v6.0.1** itself.

## System Overview

```text
ESP32-C3 SuperMini meter node
        |
        | ESP-NOW
        v
LilyGo substation / relay
        |
        | LoRa
        v
LilyGo gateway
        |
        | MQTT
        v
MQTT broker
```

## Features

- Simulated meter readings for import kWh, export kWh, voltage and battery voltage.
- ESP-NOW communication from the SuperMini node to the LilyGo substation.
- RTC memory buffering on the SuperMini for unsent records.
- LoRa relay with acknowledgement handling and retry logic.
- MQTT publishing from the gateway, with automatic reconnection.
- OLED status display on LilyGo devices.

## Hardware

- ESP32-C3 SuperMini
- LilyGo / ESP32 LoRa OLED board as the substation relay
- LilyGo / ESP32 LoRa OLED board as the gateway
- LoRa antennas
- MQTT broker

## Project Structure

Each device is a standalone ESP-IDF project under `firmware/`. They share drivers and configuration through the components in `components/`, which every project picks up via `EXTRA_COMPONENT_DIRS`.

```text
Project_Kaizen/
├── components/
│   ├── project_config/          # board pin map and secrets.h
│   ├── shared_payload/          # the on-air frame layout
│   ├── sx127x/                  # SX1276 LoRa driver
│   └── ssd1306/                 # SSD1306 OLED driver
├── firmware/
│   ├── meter_node/              # ESP32-C3 SuperMini
│   ├── substation/              # LilyGo relay
│   └── gateway/                 # LilyGo gateway
├── tools/
│   └── gen_compile_commands.py  # editor index for all three projects
└── README.md
```

| Project | Target | Purpose |
|---|---|---|
| `firmware/meter_node` | `esp32c3` | Simulates meter readings and sends them by ESP-NOW. |
| `firmware/substation` | `esp32` | Receives ESP-NOW frames and relays them by LoRa. |
| `firmware/gateway` | `esp32` | Receives LoRa frames, sends ACKs and publishes to MQTT. |

The target for each project is fixed by `board` in its `platformio.ini`, so no `set-target` step is needed.

Each firmware directory keeps ESP-IDF's own layout, not PlatformIO's: `platformio.ini` sets `src_dir = main`, so the build runs from the stock `CMakeLists.txt` and `main/` in that directory. PlatformIO only drives it; the project itself is ordinary ESP-IDF.

### Local components

- **`sx127x`** replaces the Arduino `sandeepmistry/LoRa` library. Modem settings reproduce the previous configuration exactly, so the radio link is unchanged. It receives in continuous mode rather than repeatedly re-arming single receive, which closes the window where an inbound frame could be missed.
- **`ssd1306`** replaces `Adafruit_SSD1306` and `Adafruit_GFX`, with a framebuffer and a 6x8 font.
- **`shared_payload`** carries the wire format and asserts its size at compile time.
- **`project_config`** holds `secrets.h` and the LilyGo pin map.

### Exeternal components
- **`espressif/mqtt`** fetched from the ESP Component Registry on first build into `firmware/gateway/managed_components/` (Only needed for the gateway)

## `secrets.h` Configuration

The project requires a local configuration file:

```text
components/project_config/include/secrets.h
```

Copy `secrets.example.h` next to it and fill in your own values:

```c
#pragma once

#define SECRET_WIFI_SSID "your-ssid"
#define SECRET_WIFI_PASS "your-password"

#define SECRET_MQTT_SERVER "broker.hivemq.com"
#define SECRET_MQTT_PORT 1883
#define SECRET_MQTT_TOPIC "your/mqtt/topic"

#define SECRET_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#define SECRET_LORA_BAND 433000000
```

### Configuration Fields

- `SECRET_WIFI_SSID` / `SECRET_WIFI_PASS`: network the gateway joins to reach the broker.
- `SECRET_MQTT_SERVER`: MQTT broker address.
- `SECRET_MQTT_PORT`: MQTT broker port. Standard non-TLS MQTT usually uses `1883`.
- `SECRET_MQTT_TOPIC`: MQTT topic used by the gateway when publishing meter data.
- `SECRET_MAC`: MAC address of the LilyGo substation relay. Flash the substation first, read the printed MAC address, then paste it here.
- `SECRET_LORA_BAND`: LoRa frequency in **Hz** used by both LilyGo devices. Both devices must use the same frequency, and it must be legal in your region.

> `SECRET_LORA_BAND` is now plain Hz (`433000000`), not the Arduino float form (`433E6`).

## Build and Flash

Flash in this order the first time: substation, then gateway, then meter node, since the meter node needs the substation's MAC address.

Each firmware is its own PlatformIO project, selected with `-d`:

```bash
pio run -d firmware/substation                  # build
pio run -d firmware/substation -t upload        # build and flash
pio device monitor -d firmware/substation       # serial monitor
```

Substitute `firmware/gateway` or `firmware/meter_node` as needed. Add `-t upload -t monitor` to flash and immediately open the monitor. Leave the monitor with `Ctrl-C`.

Nothing needs activating first. PlatformIO carries its own Python environment, toolchain and `esptool`, so a plain shell is enough to build, flash and monitor. ESP-IDF command equivalents are:

| `idf.py` | PlatformIO |
|---|---|
| `idf.py build` | `pio run -d firmware/<name>` |
| `idf.py flash` | `pio run -d firmware/<name> -t upload` |
| `idf.py monitor` | `pio device monitor -d firmware/<name>` |
| `idf.py menuconfig` | `pio run -d firmware/<name> -t menuconfig` |
| `idf.py fullclean` | `pio run -d firmware/<name> -t fullclean` |

### ESP-IDF version

Each `platformio.ini` pins `platform = platformio/espressif32@7.0.1`, the platform release that ships ESP-IDF v6.0.1. Any change in verstion should be escaladted

A standalone ESP-IDF install is not used and not required. These directories are still valid ESP-IDF projects, so `idf.py` works if you have it set up, but the pinned PlatformIO build is the supported path and the one the tooling below assumes.

### Notes per device

- **Substation**: copy the MAC address printed on boot into `SECRET_MAC`, then rebuild the meter node.
- **Gateway**: confirm it connects to WiFi and then to the MQTT broker.
- **Meter node**: generates simulated data, sends it by ESP-NOW, then waits out its cycle. It enumerates as a USB CDC device (`/dev/cu.usbmodem*`), not a USB-serial bridge.

### Where build settings live

Use `menuconfig` to edit `sdkconfig.<env>` files (configuration files that are local and not commited).
Most build options live in each firmware's `sdkconfig.defaults`, which PlatformIO hands to the ESP-IDF build unchanged

Two settings duplicated for paltfomIO (platformio.ini) and ESP-idf (sdkconfig.defaults)

| Setting | `platformio.ini` | `sdkconfig.defaults` |
|---|---|---|
| Chip target | `board` | `CONFIG_IDF_TARGET` |
| Partition table | `board_build.partitions` | `CONFIG_PARTITION_TABLE_*` |

PlatformIO builds the partition image it flashes from `board_build.partitions`, while the ESP-IDF side of the build sizes the app against `CONFIG_PARTITION_TABLE_*`. Both are currently `partitions_singleapp_large.csv`, a stock ESP-IDF file that resolves against the framework package. If partition image change in both settings, or the firmware is sized against one layout and flashed with another.

A file named `partitions.csv` in the firmware directory takes priority over the stock one. To make a new partition image:

1. Copy the stock CSV from `~/.platformio/packages/framework-espidf/components/partition_table/` to `firmware/<name>/partitions.csv` and edit it. Leave the offset column blank so partitions pack in order, and keep the total inside the board's 4MB flash.
2. `platformio.ini`: `board_build.partitions = partitions.csv`.
3. `sdkconfig.defaults`: replace `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` with `CONFIG_PARTITION_TABLE_CUSTOM=y` and `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`.
4. Rebuild with `-t fullclean` first. Partition sizes are baked into the build tree.
5. Verify: check the app size the build reports, or dump the flashed image back to CSV with `gen_esp32part.py firmware/<name>/.pio/build/<name>/partitions.bin`.

Commit the CSV. Past 4MB, `CONFIG_ESPTOOLPY_FLASHSIZE_*` and `board_upload.flash_size` become a third pair to keep in step.

## Payload Format

The binary payload is defined in `components/shared_payload/include/shared_payload.h`:

```c
typedef struct {
    uint32_t uid;
    uint32_t seq;
    float    kwh_import;
    float    kwh_export;
    float    voltage;
    float    battery_v;
    uint8_t  community_id;
    uint8_t  unit_id;
} payload_t;   /* 26 bytes */

typedef struct {
    uint32_t uid;
    uint32_t seq;
} ack_payload_t;  /* 8 bytes */
```

Both structs are packed, and their sizes are checked with `_Static_assert` at compile time. Receivers tell a data frame from an acknowledgement by length, so the two sizes must stay different.

All devices must use the same layout. If it changes, rebuild and flash all three firmwares.


## Troubleshooting

### ESP-NOW Send Fails

- Check that `SECRET_MAC` is the LilyGo substation MAC address.
- Confirm both devices are powered on.
- Node and substation must agree on `ESPNOW_CHANNEL` (both default to 1).

### LoRa Init Failed

- The driver reads the radio's version register on boot and refuses to continue if it does not read back `0x12`. That points at SPI wiring or the pin map in `components/project_config/include/board_lilygo.h`.

### LoRa Receive Size Mismatch

- Confirm all devices were built from the same `shared_payload.h`.
- Rebuild and flash all three firmwares after changing the payload structure.
- Confirm both LoRa devices use the same `SECRET_LORA_BAND`, spreading factor, bandwidth and sync word.

### MQTT Does Not Connect

- Check MQTT server and port.
- Confirm the broker accepts the connection.
- Avoid using MQTT wildcard characters such as `+` when publishing to a topic.
- The gateway overrides the DHCP-supplied DNS server with `8.8.8.8`, since the deployment network resolved broker hostnames unreliably. Set `FORCE_PUBLIC_DNS` to `0` in `gateway.c` to keep the network's own resolver.

### Nothing on the OLED

- Both LilyGo firmwares carry on without a display if the panel does not respond, and log a warning. Check `BOARD_OLED_SDA` / `BOARD_OLED_SCL` and the `0x3C` address.
