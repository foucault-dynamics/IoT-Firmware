# Focault Dynamics IoT Firmware
Firmware for the Focault Dynamics IoT metering network, a QUT capstone project.
The repository began as a fork of [NEXTGEN_IEMS](https://github.com/kahoQUT/NEXTGEN_IEMS),
whose SuperMini end-node plus LilyGo substation and gateway prototype is still
here as reference. The goal of this repository is to move every node onto the
ESP32-C3 SuperMini and give each one a swappable meter-reading module (RS485,
IR optical head, or camera) chosen by its role in the network.

## Project Status

Read this before flashing anything.

- **The layered architecture is merged.** A single `src/main.cpp` selects the
  node role at boot and dispatches into `src/nodes/`. This is the `unified`
  environment and it builds.
- **Modbus RTU over RS485 is implemented end to end in firmware.** The
  `ModbusRtuReader` builds requests, validates CRC and T3.5 framing, decodes
  exception codes, and returns floats. It has not been tested against a real
  SP3485 transceiver and a real meter.
- **The default build is still the legacy skeleton.** `default_envs = supermini`
  in `platformio.ini`, so a bare `pio run` gives you the old non-transmitting
  sketch, not the new architecture. Build `-e unified` explicitly.
- **Two modules are mid-refactor and uncommitted.** `lib/tcp_bus/` and
  `lib/substation/` do not compile. See [In Progress](#in-progress).

## System Overview

```text
ESP32-C3 meter node
  Sp3485 (Module)  ->  ModbusRtuReader (Reader)     <- implemented, untested on hardware
  Wifi (Transmitter)                                <- implemented, not yet called
        |
        | ESP-NOW                                   <- both sides work
        v
substation / relay                                  <- works
        |
        | LoRa (with ACK + retry)                   <- works
        v
     gateway                                        <- works
        |
        | MQTT                                      <- works (publishes raw binary)
        v
MQTT broker
```

## Architecture

Three abstractions stack on top of each other. Each layer knows only about the
one below it, so a node is assembled by picking one of each.

| Layer | Header | Responsibility |
|---|---|---|
| `Module` | `lib/module/module.h` | Moves raw bytes over one physical bus. Knows its pins and its peripheral, knows nothing about meaning. `init()`, `send()`, `readByte()`, `available()`. |
| `Reader` | `lib/reader/reader.h` | Speaks a meter protocol on top of a `Module&`. Turns registers into values. `init(Module&, const void *config)`, `get_import()`, `get_export()`, `get_voltage()`. |
| `Transmitter` | `lib/transmitter/transmitter.h` | Moves whole packets to a peer address. `init()`, `sendPacket()`, `receivePacket()`. |

Configuration is a plain struct per protocol, defined in
`lib/node_config/node_config.h`. `Reader::init()` takes it as `const void *` and
each reader casts to the struct it expects, which keeps the base interface free
of every protocol's fields. The `load*Config()` functions in `node_config.cpp`
are hardcoded seams; upstream configuration selection replaces their bodies
later without touching any call site.

Role dispatch lives in `src/main.cpp`. `readModuleType()` is currently hardcoded
to `Rs485Node` and is meant to read a hardware ID pin, which the hardware team
owns.

## What Works Today

### Unified firmware, `unified` env, `src/main.cpp` + `src/nodes/`

`setup()` reads the module type, prints it, and calls the matching
`*NodeSetup()`. `loop()` calls the matching `*Loop()`. An unknown type idles and
reminds on serial every 5 seconds.

| Node | File | State |
|---|---|---|
| `rs485_node` | `src/nodes/rs485_node.cpp` | Reads import, export, voltage over Modbus RTU every 10 s and prints them. Does not transmit. |
| `substation` | `src/nodes/substation.cpp` | Working. Verbatim port of the LilyGo sketch. |
| `gateway` | `src/nodes/gateway.cpp` | Working. Verbatim port of the gateway sketch. |
| `ir_node` | `src/nodes/ir_node.cpp` | Prints a not-implemented notice. Stack lives on `ir-module`. |
| `cv_node` | `src/nodes/cv_node.cpp` | Prints a not-implemented notice. |

### `Sp3485`, `lib/sp3485/`

RS485 transceiver as a `Module`. Drives DE/RE high to transmit, flushes the UART
so the last bit physically leaves the wire, then drops the line back to receive.
Drains stale RX bytes before every send so a late reply cannot desync the next
frame.

### `ModbusRtuReader`, `lib/modbus_rtu/`

Function code 0x03 (Hardcoded) reads of two consecutive registers.

- `init()` derives the T3.5 inter-frame gap from the `SerialConfig`, parsing data
  bits, parity, and stop bits out of the Arduino format bitmask. Above 19200
  baud it uses the fixed 1750 us the spec mandates.
- `read_register()` waits out T3.5, sends the request, then reads until T3.5 of
  silence closes the response frame or the 500 ms timeout expires.
- Validates length, slave address, CRC16, function code, and byte count, and
  decodes the eleven standard exception codes to serial.
- Decodes the 32-bit result as either a scaled integer (divided by 1000) or an
  IEEE 754 float, selected by `RegisterFormat` in the config.

Not yet exercised against real hardware. It has been developed against
`ModbusSim/`.

### `Wifi`, `lib/wifi/`

ESP-NOW as a `Transmitter`. This is the complete send and receive path the old
SuperMini skeleton never had.

- Brings up WiFi as station or access point, optionally pins the channel.
- Received frames are copied in the callback into a FreeRTOS queue
  (`ESPNOW_RX_QUEUE_DEPTH` of 4), so `receivePacket()` never runs in interrupt
  context. Returns the frame length, `-1` when the queue is empty, `-2` when the
  caller's buffer is too small.
- `sendPacket()` blocks on a binary semaphore given by the send callback, so it
  returns only once the radio has confirmed or timed out on delivery.
- `addPeer()` registers a peer from an `EspNowPeerConfig`.

### Legacy sketches, `supermini` / `lilygo` / `gateway` envs

The three original single-file firmwares are still on `main` and still build.
`main-esp-now-lilyGo.cpp` and `main-esp-now-gateway.cpp` are the sources the
substation and gateway nodes were ported from, kept as reference.
`main-esp-now-supermini.cpp` is the non-transmitting skeleton described in
[PlatformIO Environments](#platformio-environments); it is superseded by
`rs485_node`.

Note: the LilyGo pin comment says "LoRa & OLED Pins", but there is no OLED code
in this repository. Nothing drives a display.

## In Progress

### Uncommitted modules

Both are present in the working tree, both are unfinished, and neither is
included by any built source, so PlatformIO's dependency finder never compiles
them and the build stays green.

| Path | State |
|---|---|
| `lib/tcp_bus/` | A `Module` carrying  RTU framing over a `WiFiClient` socket, matching `ModbusSimTCP.py`. Logic is written but it needs `TcpBusConfig`, which exists on `origin/RS485` and not on `main`. Will not compile until that struct is merged. |
| `lib/substation/` | Skeleton only. `substation.h` has a syntax error (`class Substation :: public Transmitter`), the three method bodies are empty, and the signatures do not match the `Transmitter` base. |

### Meter-reading modules not yet started

| Module | State | Missing include |
|---|---|---|
| `lib/esp32cam/` | Empty. The `.cpp` is two includes and a TODO, with no method bodies | `cam_link_protocol.h` |
| `lib/ir_head/` | Header only, and it declares nothing. Comment block ends "decide the modulation scheme and fill in the class" | `pin_config.h` |

### Unmerged work on other branches

`RS485` has been merged; it now carries only the two commits below on top of
`main`. `Substation` is fully merged.

| Branch | Ahead of `main` | Carries |
|---|---|---|
| `RS485` | 2 | `TcpBusConfig` plus `loadTcpBusConfig()`, Modbus-over-TCP wiring in `rs485_node`, a boot delay so serial output is not missed, and simulator tweaks. This is what `lib/tcp_bus/` needs. |
| `lora` | 4 | `lib/lora/loramodule.{h,cpp}`, a LoRa packet transport |
| `ir-module` | 2 (and 13 behind) | `lib/iec62056_21/`, real and simulated IR head implementations, `lib/uart/`, `lib/sx1276/`, `lib/shared/pin_config.h`, `lib/shared/cam_link_protocol.h` |
| `ESP-IDF-migration` | 5 (and 21 behind) | Exploratory ESP-IDF port |

## Repository Layout

```text
Project_Kaizen/
├── lib/
│   ├── module/          Module base class
│   ├── reader/          Reader base class
│   ├── transmitter/     Transmitter base class
│   ├── node_config/     Per-protocol config structs and the hardcoded loaders
│   ├── shared/          shared_payload.h
│   ├── sp3485/          RS485 transceiver Module
│   ├── modbus_rtu/      Modbus RTU Reader
│   ├── wifi/            ESP-NOW Transmitter
│   ├── tcp_bus/         RTU-over-TCP Module (WIP, does not compile)
│   ├── substation/      LoRa Transmitter (WIP, does not compile)
│   ├── esp32cam/        empty
│   └── ir_head/         empty
├── src/
│   ├── main.cpp                       role dispatch
│   ├── nodes/
│   │   ├── nodes.h
│   │   ├── rs485_node.cpp
│   │   ├── substation.cpp
│   │   ├── gateway.cpp
│   │   ├── ir_node.cpp
│   │   └── cv_node.cpp
│   ├── main-esp-now-supermini.cpp     legacy
│   ├── main-esp-now-lilyGo.cpp        legacy
│   ├── main-esp-now-gateway.cpp       legacy
│   └── secrets.h
├── ModbusSim/
│   ├── ModbusSimSerial.py
│   ├── ModbusSimTCP.py
│   ├── poll_test.py
│   └── README.md
├── test/
├── platformio.ini
└── README.md
```

## Directory Guide

What each directory is for, and where to look when you need to change something.

### `src/`

The firmware entry points. `main.cpp` holds `readModuleType()` and the two
`switch` statements that dispatch into a role; it is the only file that knows all
five roles exist.

`src/nodes/` holds one `.cpp` per role, each exposing just a `*Setup()` and a
`*Loop()` through `nodes.h`. This is the assembly layer: a node file is where a
`Module`, a `Reader`, and a `Transmitter` get picked, constructed from a config,
and wired together. Nothing in `lib/` knows which node uses it.

`secrets.h` holds credentials and the substation MAC. The three
`main-esp-now-*.cpp` files are the original single-file sketches, kept as
reference for what the ported nodes were derived from.

### `lib/`

Every reusable piece of the firmware, one subdirectory per library, compiled by
PlatformIO into separate static libraries. It splits into the three abstract base
classes (`module/`, `reader/`, `transmitter/`), the configuration structs
(`node_config/`, `shared/`), and the concrete implementations (`sp3485/`,
`modbus_rtu/`, `wifi/`, and the unfinished `tcp_bus/`, `substation/`, `ir_head/`,
`esp32cam/`).

PlatformIO only compiles a library that something includes, which is why the
unfinished ones do not break the build. **See `lib/README` for a description of
each subdirectory** and how the layers fit together.

### `ModbusSim/`

Python `pymodbus` servers that impersonate a meter, plus a client to poll them.
This is how the Modbus reader is developed without hardware. See
`ModbusSim/README.md` for the register map, setup, and the encoding quirk to be
aware of.

### `test/`

PlatformIO Test Runner directory. Empty apart from the stock placeholder README.
No tests exist yet.

### Generated, not source

`.pio/` is PlatformIO's build output. `compile_commands.json` and the per-
environment copies under `.compile_commands/` are the clangd compilation
database, regenerated with `pio run -t compiledb`. `.cache/` is clangd's index.
None of it is tracked; the `.compile_commands/` directory itself is not named in
`.gitignore`, but everything inside it is a `compile_commands.json`, which is.


## PlatformIO Environments

Every environment targets `board = esp32-c3-devkitm-1`. There is no LilyGo board
id in `platformio.ini`, so the `lilygo` and `gateway` names describe the role and
the physical board the code was written against, not the build target.

| Environment | Builds | Status |
|---|---|---|
| `unified` | `main.cpp`, `nodes/` | **Builds.** The current architecture. Role is hardcoded to `Rs485Node`. |
| `supermini` (default) | `main-esp-now-supermini.cpp` | Builds. Legacy skeleton, transmits nothing. |
| `lilygo` | `main-esp-now-lilyGo.cpp` | Builds. Legacy, superseded by the `substation` node. |
| `gateway` | `main-esp-now-gateway.cpp` | Builds, with the serial caveat below. Legacy, superseded by the `gateway` node. |
| `modbus_node` | `main_modbus.cpp` | **Cannot build.** The file was deleted from `main` by commit `304c27a`. The environment should be removed. |

The `gateway` environment sets no `build_flags`, so unlike the others it is built
without `ARDUINO_USB_MODE` and `ARDUINO_USB_CDC_ON_BOOT`. Serial output will not
appear over native USB on that board. Expect a silent monitor. `unified` sets
both flags, so the ported gateway node does not have this problem.

## Hardware

- ESP32-C3 SuperMini as the meter end-node
- LilyGo / ESP32 LoRa OLED board as the substation relay and as the gateway,
  inherited from NEXTGEN_IEMS. The OLED is unused.
- SX1276 LoRa module and antennas
- SP3485 RS485 transceiver on the meter node
- An MQTT broker

RS485 pins come from `loadModbusRtuConfig()` in `lib/node_config/node_config.cpp`:
`RX 8`, `TX 9`, `DE/RE 10`, at 9600 baud `SERIAL_8N1`.

LoRa pins are identical in the substation and gateway and are still `#define`d
separately in each file rather than shared: `SCK 4`, `MISO 5`, `MOSI 6`, `SS 7`,
`RST 3`, `DIO0 1`.

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
- `SECRET_MAC`: MAC address of the substation relay. Flash the substation first,
  read the MAC it prints on boot, and paste it here. `rs485_node` reads this into
  its `EspNowPeerConfig`.
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

`rs485_node` currently fills `kwh_import`, `kwh_export`, and `voltage` only.
`uid`, `seq`, `battery_v`, `community_id`, and `unit_id` are never set.

## Build and Flash

Flash the substation first, because you need its MAC address for `SECRET_MAC`.
All three roles come out of the same `unified` build; change the return value of
`readModuleType()` in `src/main.cpp` before each flash until the ID pin exists.

```bash
# 1. Set readModuleType() to ModuleType::Substation
pio run -e unified -t upload
pio device monitor -e unified
```

Copy the printed MAC into `SECRET_MAC` in `src/secrets.h`, then:

```bash
# 2. Set readModuleType() to ModuleType::Gateway, flash the gateway board
pio run -e unified -t upload

# 3. Set readModuleType() to ModuleType::Rs485Node, flash the meter node
pio run -e unified -t upload
pio device monitor -e unified
```

The meter node prints its three readings every 10 seconds. It does not send them
onwards yet, so to exercise the ESP-NOW, LoRa, and MQTT path end to end you still
have to inject a packet yourself.

Note that `pio run` with no `-e` builds `supermini`, the legacy skeleton. Always
pass `-e unified`.

## Modbus Simulator

`ModbusSim/` holds two `pymodbus` servers used to develop the meter-reading side
without physical hardware. Both expose a three-value register map, voltage at
address 0, export kWh at 2, import kWh at 4, each a 32-bit big-endian pair, with
values that change on every poll. These addresses match the
`MeterModel::Simulated_Serial` and `MeterModel::Simulated_Tcp` rows in
`node_config.cpp`.

- `ModbusSimTCP.py` serves RTU-over-TCP on `0.0.0.0:5020`. This is what
  `lib/tcp_bus/` talks to.
- `ModbusSimSerial.py` serves RTU on a serial port, hardcoded to `/dev/ttyUSB0`.
- `poll_test.py` is a minimal RTU-over-TCP client that reads all three registers
  and decodes them, useful for confirming the server before pointing firmware at
  it.

See `ModbusSim/README.md` for venv setup, how to poll with
`python3 -m pymodbus.console`, and the `socat` null-modem recipe for the serial
variant.

## Known Gaps

Recorded rather than fixed. Worth clearing before submission.

- `rs485_node` reads but never transmits. `Wifi::sendPacket()` is implemented and
  the peer is registered, but nothing calls it.
- `readModuleType()` in `src/main.cpp` is hardcoded. Every role change needs a
  source edit and a reflash until the hardware ID pin lands.
- The RS485 path has not been tested against a real SP3485 and a real meter, only
  against the simulator.
- `default_envs = supermini` still points at the legacy skeleton.
- The `modbus_node` environment references a file that no longer exists and fails
  immediately.
- `lib/tcp_bus/` and `lib/substation/` are uncommitted and do not compile.
- No tests. `test/` holds only the stock PlatformIO placeholder.
- `src/secrets.h` is committed with live credentials.
- `platformio.ini` lists `knolleary/PubSubClient @ ^2.8` twice under `[env:gateway]`.
- `.gitignore` has `ModBusSim/.venv`, capitalised differently from the actual
  `ModbusSim/` directory. It matches on macOS only because git is case-insensitive
  there by default; on Linux or CI the virtualenv would stop being ignored. It
  also does not ignore `ModbusSim/__pycache__/`.
- `onLoRaReceive()` in the gateway node is defined but never registered with
  `LoRa.onReceive()`. It is dead code left from an interrupt-based approach that
  polling replaced.
- LoRa pin definitions are duplicated between the substation and gateway nodes
  instead of living in `node_config.h` like the RS485 pins.
- `Sp3485::readByte()` calls `pinMode(derePin, INPUT)` before driving it low,
  which releases the pin rather than driving the transceiver into receive. The
  send path already leaves DE/RE low, so this is redundant at best.
- In the simulators, `define_device()` seeds the kWh registers as `FLOAT32` but
  `meter_action()` re-encodes them as `uint32`, so their representation changes
  after the first poll. The firmware's `RegisterFormat::ScaledInt` matches the
  post-poll encoding, not the seeded one.

## Troubleshooting

**ESP-NOW send fails.** Check `SECRET_MAC` is the substation's MAC. Confirm both
devices are powered and on the same channel. `EspNowConfig::channel` of 0 means
"use whatever the interface is already on", so if one side pins a channel and the
other does not, delivery fails silently until the send timeout.

**Modbus response incomplete or CRC mismatch.** Confirm baud rate and
`SerialConfig` match the meter, since T3.5 is derived from them. Check the DE/RE
pin is wired to `cfg.bus.dere` (GPIO 10 by default). Point the node at
`ModbusSim` first to separate a protocol problem from a wiring problem.

**Readings are 1000x off.** `RegisterFormat` in `loadModbusRtuConfig()` is set to
`ScaledInt`, which divides the raw 32-bit value by 1000. Meters that publish IEEE
754 floats need `IEEE_754Float`.

**LoRa receive size mismatch.** Confirm every device was built from the same
`shared_payload.h`, and that both LoRa devices use the same `SECRET_LORA_BAND`.
Rebuild and reflash all environments after any payload change.

**MQTT does not connect.** Check the server and port, confirm the broker accepts
anonymous connections, and avoid wildcard characters such as `+` in the publish
topic.

**No serial output from the gateway.** Expected on native USB with the legacy
`gateway` environment, which is built without USB CDC. Use `unified` instead, or
an external USB-to-serial adapter, or add the two `ARDUINO_USB_*` build flags.

**Nothing on serial right after boot.** The `unified` build prints its banner
immediately, before USB CDC has enumerated, so the first lines are easy to miss.
Attach the monitor before resetting the board.
