# Focault Dynamics IoT Firmware

Firmware for the Focault Dynamics IoT metering network, a QUT capstone project.
The repository began as a fork of [NEXTGEN_IEMS](https://github.com/kahoQUT/NEXTGEN_IEMS),
whose SuperMini end-node plus LilyGo substation and gateway prototype is still
here as reference. The goal of this repository is to move every node onto the
ESP32-C3 SuperMini and give each one a swappable meter-reading module (RS485,
IR optical head, or camera) chosen by its role in the network.

## Project Status

Read this before flashing anything.

- **The radio transport chain works.** ESP-NOW into the substation, LoRa with
  acknowledgement and retry out to the gateway, MQTT publish from the gateway.
- **No meter reading is implemented on `main`.** Nothing populates a `Payload`.
  No Modbus transaction is ever executed and no RS485 byte is ever sent.
- **The default build (`supermini`) does not transmit.** It initialises ESP-NOW,
  registers a peer, and stops. See [What Works Today](#what-works-today).
- **The RS485 refactor is unmerged.** It lives on `origin/RS485` and is the main
  line of in-progress work. See [In Progress](#in-progress).

## System Overview

```text
ESP32-C3 meter node (RS485 / IR / CV)     <- planned, not implemented
        |
        | ESP-NOW                          <- receive side works, send side is a skeleton
        v
substation / relay                         <- works
        |
        | LoRa (with ACK + retry)          <- works
        v
     gateway                               <- works
        |
        | MQTT                             <- works (publishes raw binary)
        v
MQTT broker
```

## What Works Today

### Substation relay, `lilygo` env, `src/main-esp-now-lilyGo.cpp`

Receives ESP-NOW packets and relays them over LoRa.

- Rejects any ESP-NOW frame whose length is not `sizeof(Payload)`.
- `sendLoRaWithAck()` retries up to 3 times with a 1500 ms ACK timeout and a
  500 ms backoff, matching the returned `uid` and `seq`.
- LoRa parameters: SF10, 125 kHz bandwidth, sync word `0xF3`, CRC on, TX power 14.
- Prints its own MAC address at boot, which is what you paste into `SECRET_MAC`.
- Restarts itself if `LoRa.begin()` fails.

Note: the pin comment says "LoRa & OLED Pins", but there is no OLED code in this
repository. Nothing drives a display.

### Gateway, `gateway` env, `src/main-esp-now-gateway.cpp`

Receives LoRa packets, acknowledges them, and republishes to MQTT.

- Joins WiFi, then forces DNS to `8.8.8.8`.
- Connects to the broker anonymously, with no username or password.
- Polls with `LoRa.parsePacket()`. On a length match it logs RSSI and SNR, sends
  an `AckPayload` back, then publishes.
- **Publishes the raw packed `Payload` struct as binary**, not JSON. Anything
  subscribing must unpack the 26-byte layout itself.
- Despite the `main-esp-now-` filename prefix, this firmware uses no ESP-NOW.

### Meter node, `supermini` env, `src/main-esp-now-supermini.cpp`

**This is a skeleton and it is also the default environment**, so it is the first
thing you get from a bare `pio run -t upload`.

`setup()` starts serial, brings up WiFi in station mode, initialises ESP-NOW,
registers a send callback, and adds the substation as a peer. It then returns.
`loop()` is empty. There is no call to `esp_now_send()` anywhere in the file, so
the node transmits zero packets.

The RTC-memory buffer (`readingBuffer`, `bufferCount`, `cumulativeImport`,
`messageCounter`), the `SLEEP_TIME_SEC` constant, and the `ackReceived` /
`deliverySuccess` flags are all declared but never used. They mark the intended
design, not working behaviour.

To exercise the LoRa and MQTT path end to end, you currently have to inject a
packet yourself rather than rely on this node.

## In Progress

### Meter-reading modules, `lib/`

`lib/module/module.h` defines an abstract `Module` base with pure virtual
`setup()`, `send()`, `receive()`, and `available()`. The concrete modules below
are scaffolding at various stages. **None of them is referenced by any built
source**, and four would not compile if they were, because commit `304c27a`
removed headers they still include.

| Module | State | Missing include |
|---|---|---|
| `lib/shared/shared_payload.h` | Complete and in use by all three firmwares | — |
| `lib/module/module.h` | Complete, abstract base only | — |
| `lib/modbus_rtu/` | Partial. `init()` is real and computes the T3.5 inter-frame gap from the serial config. `get_import()` falls off the end without returning; `get_export()` and `get_voltage()` have empty bodies | `node_config.h`, `reader.h` |
| `lib/sp3485/` | Stub. `setup()` calls `Serial1.begin()`, the other methods return success without doing anything | `pin_config.h` |
| `lib/esp32cam/` | Empty. The `.cpp` is two includes and a TODO, with no method bodies | `cam_link_protocol.h` |
| `lib/ir_head/` | Header only, and it declares nothing. Comment block ends "decide the modulation scheme and fill in the class" | `pin_config.h` |

### Unmerged work on other branches

The architecture this repository is heading towards, a single `main.cpp` with the
node role selected at build time, exists on `origin/RS485` and was never merged.
That branch is 4 commits ahead and 4 behind `main`.

| Branch | Carries |
|---|---|
| `RS485` | `src/main.cpp`, `src/nodes/` (gateway, substation, rs485_node, ir_node, cv_node), `lib/node_config/`, `lib/reader/` |
| `ir-module` | `lib/iec62056_21/`, real and simulated IR head implementations, `lib/uart/`, `lib/sx1276/`, `lib/shared/pin_config.h` |
| `lora` | A `lib/lora/` module |
| `Substation` | Oldest layout, payload still in `include/SharedPayload.h` |
| `ESP-IDF-migration` | Exploratory ESP-IDF port |

## Repository Layout

```text
Project_Kaizen/
├── lib/
│   ├── esp32cam/
│   ├── ir_head/
│   ├── modbus_rtu/
│   ├── module/
│   ├── shared/
│   └── sp3485/
├── src/
│   ├── main-esp-now-supermini.cpp
│   ├── main-esp-now-lilyGo.cpp
│   ├── main-esp-now-gateway.cpp
│   └── secrets.h
├── ModbusSim/
│   ├── ModbusSimSerial.py
│   ├── ModbusSimTCP.py
│   └── README.md
├── test/
├── platformio.ini
└── README.md
```

## PlatformIO Environments

Every environment targets `board = esp32-c3-devkitm-1`. There is no LilyGo board
id in `platformio.ini`, so the `lilygo` and `gateway` names describe the role and
the physical board the code was written against, not the build target.

| Environment | Builds | Status |
|---|---|---|
| `supermini` (default) | `main-esp-now-supermini.cpp` | Builds. Transmits nothing, see above. |
| `lilygo` | `main-esp-now-lilyGo.cpp` | Working. |
| `gateway` | `main-esp-now-gateway.cpp` | Working, with the serial caveat below. |
| `modbus_node` | `main_modbus.cpp` | **Cannot build.** The file was deleted from `main` by commit `304c27a`. |
| `unified` | `main.cpp`, `nodes/` | **Cannot build.** Neither path exists on `main`; both are on `origin/RS485`. |

The `gateway` environment sets no `build_flags`, so unlike the others it is built
without `ARDUINO_USB_MODE` and `ARDUINO_USB_CDC_ON_BOOT`. Serial output will not
appear over native USB on that board. Expect a silent monitor.

## Hardware

- ESP32-C3 SuperMini as the meter end-node
- LilyGo / ESP32 LoRa OLED board as the substation relay and as the gateway,
  inherited from NEXTGEN_IEMS. The OLED is unused.
- SX1276 LoRa module and antennas
- SP3485 RS485 transceiver, for the unimplemented meter module
- An MQTT broker

LoRa pins are identical in both firmwares and are `#define`d separately in each
file rather than shared: `SCK 4`, `MISO 5`, `MOSI 6`, `SS 7`, `RST 3`, `DIO0 1`.

## `secrets.h` Configuration

The firmware expects `src/secrets.h`:

```cpp
#pragma once

#define SECRET_WIFI_SSID "your-ssid"
#define SECRET_WIFI_PASS "your-password"

#define SECRET_MQTT_SERVER "broker.hivemq.com"
#define SECRET_MQTT_PORT 1883
#define SECRET_MQTT_TOPIC "your/mqtt/topic"

#define SECRET_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#define SECRET_LORA_BAND 433E6
```

- `SECRET_WIFI_SSID`, `SECRET_WIFI_PASS`: used by the gateway to reach the broker.
- `SECRET_MQTT_SERVER`, `SECRET_MQTT_PORT`: broker address and port. Non-TLS MQTT
  is usually `1883`.
- `SECRET_MQTT_TOPIC`: topic the gateway publishes to. Avoid wildcard characters.
- `SECRET_MAC`: MAC address of the substation relay. Flash `lilygo` first, read
  the MAC it prints on boot, and paste it here.
- `SECRET_LORA_BAND`: LoRa frequency. Both LoRa devices must match.

**This file is currently tracked in git with live values.** It should be added to
`.gitignore` and the committed copy replaced with a `secrets.example.h`.

## Payload Format

Defined in `lib/shared/shared_payload.h`, packed to 26 bytes:

```cpp
#pragma pack(push, 1)
struct Payload {
    uint32_t uid;           // Node/Device ID
    uint32_t seq;           // Sequence number for deduplication
    float kwh_import;       // 1.8.0 value
    float kwh_export;       // 2.8.0 value
    float voltage;          // Grid voltage
    float battery_v;        // ESP32 battery level
    uint8_t community_id;
    uint8_t unit_id;
};

struct AckPayload {
    uint32_t uid;
    uint32_t seq;
};
#pragma pack(pop)
```

Every device must use the same struct. Both the ESP-NOW receive path and the LoRa
receive path reject frames on a `sizeof()` mismatch, so if you change this file,
rebuild and reflash all environments together.

## Build and Flash

Flash the substation first, because you need its MAC address for `SECRET_MAC`.

```bash
pio run -e lilygo -t upload
pio device monitor -e lilygo
```

Copy the printed MAC into `SECRET_MAC` in `src/secrets.h`, then flash the gateway:

```bash
pio run -e gateway -t upload
pio device monitor -e gateway    # may stay silent, see the env notes above
```

Then the meter node:

```bash
pio run -e supermini -t upload
pio device monitor -e supermini
```

The SuperMini will print its banner and go idle. It does not yet send data.

## Modbus Simulator

`ModbusSim/` holds two `pymodbus` servers used to develop the meter-reading side
without physical hardware. Both expose a three-value register map, voltage at
address 0, export kWh at 2, import kWh at 4, each a 32-bit big-endian pair, with
values that change on every poll.

- `ModbusSimTCP.py` serves RTU-over-TCP on `0.0.0.0:5020`.
- `ModbusSimSerial.py` serves RTU on a serial port, hardcoded to `/dev/ttyUSB0`.

See `ModbusSim/README.md` for venv setup, how to poll with
`python3 -m pymodbus.console`, and the `socat` null-modem recipe for the serial
variant.

## Known Gaps

Recorded rather than fixed. Worth clearing before submission.

- No tests. `test/` holds only the stock PlatformIO placeholder.
- `src/secrets.h` is committed with live credentials.
- `platformio.ini` lists `knolleary/PubSubClient @ ^2.8` twice under `[env:gateway]`.
- `.gitignore` has `ModBusSim/.venv`, capitalised differently from the actual
  `ModbusSim/` directory. It matches on macOS only because git is case-insensitive
  there by default; on Linux or CI the virtualenv would stop being ignored.
- `onLoRaReceive()` in the gateway is defined but never registered with
  `LoRa.onReceive()`. It is dead code left from an interrupt-based approach that
  polling replaced.
- LoRa pin definitions are duplicated between the two firmwares instead of shared.
- In the simulators, `define_device()` seeds the kWh registers as `FLOAT32` but
  `meter_action()` re-encodes them as `uint32`, so their representation changes
  after the first poll.

## Troubleshooting

**ESP-NOW send fails.** Check `SECRET_MAC` is the substation's MAC. Confirm both
devices are powered. Use baud `115200`.

**LoRa receive size mismatch.** Confirm every device was built from the same
`shared_payload.h`, and that both LoRa devices use the same `SECRET_LORA_BAND`.
Rebuild and reflash all environments after any payload change.

**MQTT does not connect.** Check the server and port, confirm the broker accepts
anonymous connections, and avoid wildcard characters such as `+` in the publish
topic.

**No serial output from the gateway.** Expected on native USB, the `gateway`
environment is built without USB CDC. Use an external USB-to-serial adapter, or
add the two `ARDUINO_USB_*` build flags that the other environments set.
