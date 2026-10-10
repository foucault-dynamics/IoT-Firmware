# lib/

Project-private libraries. PlatformIO compiles each subdirectory into its own
static library and links it into the firmware. The Library Dependency Finder
scans the source files for `#include`s and pulls in only what is actually
reachable, so a subdirectory that nothing includes is never compiled. That is
why the work-in-progress modules below do not break the build.

Each library lives in `lib/<group>/<name>/` as a flat `.h` plus `.cpp` pair.
More on the LDF: https://docs.platformio.org/page/librarymanager/ldf.html

```text
lib/
  interfaces/  module, reader, transmitter, shared
  config/      node_config, nvs_config, secrets
  buses/       sp3485, http_bus, tcp_bus, lora, ir_head
  protocols/   modbus_rtu, dlms_cosem, cam_http, iec62056_21, payload_json
  links/       esp_now_uplink, lora_link, wifi_radio
  buffering/   seq_counter, reading_buffer
```

PlatformIO only looks one level deep, so on its own it would see each group
folder as a single library. `lib_extra_dirs` in `platformio.ini` lists every
group folder as a separate place to search, which keeps each module its own
library. A new group folder must be added to that list, otherwise the modules
inside it are never found and their headers fail to resolve.

## The three abstractions

Everything here fits into one of three layers, or is the configuration that
feeds them. A node is assembled by picking one of each.

```text
Reader        protocol meaning   (Modbus RTU registers -> floats)
   | holds a Module&
Module        raw bytes on a bus (SP3485, TCP socket, IR head)

Transmitter   whole packets to a peer address (ESP-NOW, LoRa)
```

`Module` and `Transmitter` are siblings, not stacked. A meter node holds one of
each: a `Module` facing the meter, a `Transmitter` facing the substation.

## Interfaces

### `module/`

`Module`, the base class for anything that moves raw bytes over one physical
bus. It knows its pins and its peripheral and nothing about what the bytes mean.
`init()` (returns `EXIT_SUCCESS`/`EXIT_FAILURE`), `send(data, len)`,
`readByte()` (returns the byte, or `-1` if none is waiting), `available()`.

### `reader/`

`Reader`, the base class for a meter protocol sitting on top of a `Module&`.
`init(Module&)`, `get_import()`, `get_export()`, `get_voltage()`, all writing
through a `float*` and returning `EXIT_SUCCESS` or `EXIT_FAILURE`.

Each concrete reader takes its config in its own constructor (`ModbusRtuReader(const
ModbusRtuConfig&)`, `DlmsCosemReader(const DlmsCosemConfig&)`,
`CamHttpReader(const CamHttpConfig&)`), storing it by value.
That keeps every protocol's fields out of the shared base interface without an
unchecked cast, at the cost of the caller in `src/nodes/` having to construct the
right reader for its config.

### `transmitter/`

`Transmitter`, the base class for anything that moves a whole packet to a peer
address. `init()`, `sendPacket(address, buf, len)`,
`receivePacket(address, buf, bufLen)`. The address is `const void *` for the same
reason the reader config is: an ESP-NOW address is a 6-byte MAC, a LoRa address
will be something else.

## Configuration

### `node_config/`

Holds only shapes, one header per node plus one for the networking structs
more than one node uses:

| Header | Structs |
|---|---|
| `networking_config.h` | `SoftApConfig`, `EspNowConfig`, `EspNowPeerConfig`, `LoRaConfig` and its `LoRaPins`, `LoRaLinkConfig` |
| `rs485_config.h` | `ReaderType`, `MeterModel`, `RegisterFormat`, `Rs485Config`, `TcpBusConfig`, `ModbusRtuConfig`, `DlmsCosemConfig`, `Rs485NodeConfig` |
| `ir_config.h` | `IrConfig`, `Iec62056Config`, `IrNodeConfig` |
| `cv_config.h` | `HttpBusConfig`, `CamHttpConfig`, `CvNodeConfig` |
| `substation_config.h` | `SubstationConfig` |
| `gateway_config.h` | `WifiStationConfig`, `MqttConfig`, `GatewayConfig` |

Each lib includes the header for the node it serves. A struct moves to
`networking_config.h` only once a second node uses it. No values, no loaders:
those live in `nvs_config/`.

### `nvs_config/`

Fills the `node_config/` structs from NVS. Defaults come from `secrets/`. The NVS
keys are listed in `NVS_KEYS.md`.

Each node has its own loader file here (`rs485_loader.cpp`,
`ir_loader.cpp`, `cv_loader.cpp`, `substation_loader.cpp`,
`gateway_loader.cpp`) holding that node's defaults, NVS keys and board pins;
`rs485_loader.cpp` also holds the `METER_MODELS` table mapping a `MeterModel`
to its register addresses, and the `DLMS_*_OBIS` constants naming the import,
export and voltage Registers the DLMS/COSEM reader asks for. A field is read from NVS if it was ever set there,
falling back to its firmware default otherwise, so a fresh board runs on
defaults and a firmware update can still improve them. `lora_loader.cpp` holds
the LoRa radio and link loaders shared by the substation and gateway loaders.
`nvs_config.cpp` holds the NVS read helpers, declared in `nvs_read.h`, plus
`nvsConfigPollSerial()`,
a serial command stand-in for the upstream config channel a future team will
replace it with.

### `secrets/`

`secrets.h`, the credentials and default addresses the NVS loaders fall back
to. It is a library rather than a file in `src/` because a library cannot include
headers from `src/`.

### `shared/`

`shared_payload.h`, the 26-byte packed `Payload` and 8-byte `AckPayload`. Every
node in the network must be built from the same copy of this header; both the
ESP-NOW and the LoRa receive paths reject frames on a `sizeof()` mismatch.

## Implementations

### `sp3485/`

`Sp3485 : Module`. The SP3485 RS485 transceiver over a `HardwareSerial`.

Half duplex, so the DE/RE pin has to be driven high to talk and dropped back low
to listen, and only after the last bit has physically left the UART. `send()`
drains stale RX bytes first so a late reply to a timed-out poll cannot desync the
next frame, raises DE/RE, writes, flushes, then lowers DE/RE.

### `modbus_rtu/`

`ModbusRtuReader : Reader`. Function code 0x03 reads of two consecutive
registers, which is the shape every value in the meter register map takes.

`init()` derives the T3.5 inter-frame gap from the `SerialConfig` bitmask,
parsing out data bits, parity, and stop bits, and uses the fixed 1750 us the spec
mandates above 19200 baud. `read_register()` waits out T3.5, sends, then reads
until T3.5 of silence closes the frame or the 500 ms timeout expires. It
validates length, slave address, CRC16, function code, and byte count, and
decodes the eleven standard exception codes to serial. The 32-bit result is
interpreted as a scaled integer or an IEEE 754 float depending on
`RegisterFormat`.

Developed against `ModbusSim/`. Not yet tested against real hardware.

### `dlms_cosem/`

`DlmsCosemReader : Reader`, for DLMS/COSEM meters on the same `Sp3485` bus,
selected by `reader = 5` (`ReaderType::DlmsCosem`). `reader = 6`
(`ReaderType::DlmsTcp`) runs the same reader on a `TcpBus` instead, against
`DlmsSim/DlmsSimTCP.py`, the way `ModbusTCP` runs against ModbusSim. Two files:

- `hdlc.h` / `hdlc.cpp` are plain functions for the HDLC link layer (IEC
  62056-46): the FCS (CRC16/X.25), encoding 1, 2 or 4 byte addresses, and
  building and parsing type A frames with their HCS and FCS checks. They know
  nothing about the bus, so the frame tests run on them directly.
- `dlms_cosem.h` / `dlms_cosem.cpp` hold the reader, which drives the HDLC
  link and speaks COSEM on top of it.

The client uses no authentication and no ciphering, with Logical Name
referencing, which is what the public client (SAP 16) gets on most meters.
Every getter runs one whole session for its Register:

1. SNRM, expecting UA. The I-frame counters V(S) and V(R) are reset only once
   the meter accepts.
2. AARQ in an I-frame, expecting an AARE that accepts the association with an
   xDLMS InitiateResponse. A rejection logs its result source diagnostic.
3. GET attribute 3, `scaler_unit`. The unit must be Wh for import and export,
   V for voltage, so a misconfigured OBIS code fails instead of reporting the
   wrong quantity.
4. GET attribute 2, `value`, decoded from any A-XDR integer or float32 into a
   `double` and multiplied by 10 to the power of the scaler.
5. DISC, sent whenever the link came up, even after a failed step, so the
   meter is never left holding an open link until its inactivity timeout.

Import and export are divided by 1000 to give the kWh the `Reader` interface
and the payload expect.

`readFrame()` hunts for a flag followed by a type A format byte, skipping noise
and repeated idle flags, then reads exactly as many bytes as the length field
gives, within the 1000 ms `DLMS_TIMEOUT`. Every I-frame exchange checks N(S)
and N(R) against the counters and the LLC header (`E6 E6 00` out, `E6 E7 00`
back). Segmented replies and block transfers are rejected, which holds as long
as every APDU fits the default 128 byte info field. Each GET failure decodes its
data-access-result to serial.

The client and server addresses come from NVS (`dlms_client`, `dlms_logical`,
`dlms_physical`, `dlms_addr_len`), the OBIS codes from `rs485_loader.cpp`.
Tested in QEMU against scripted frames. Not yet tested against a real meter.

### `wifi_radio/`

Plain functions, not a class: `wifiRadioStartAp()`, `wifiRadioStartStation()`,
`wifiRadioApUp()`, `wifiRadioHasStations()`. Owns everything below TCP on the
C3's one radio (mode, channel, tx power, softAP), so `HttpBus` and
`EspNowUplink` never configure the radio themselves, they only ask it to start.
The two starts add to each other (`WIFI_AP_STA`) rather than replace each
other, so they work in either order, and both refuse a channel different from
the one the radio is already on. `EspNowUplink::init()` starts the station
itself on `EspNowConfig::channel` and always registers peers on the station
interface, so ESP-NOW only nodes (IR, RS485 RTU) never touch this library.
Nodes that host a network (CV, RS485 in ModbusTCP or DlmsTcp mode) also call
`wifiRadioStartAp()` on that same channel.

### `http_bus/`

`HttpBus : Module`. HTTP client over the AP that `wifi_radio` hosts, for the
ESP32-CAM (AI-on-the-edge-device). `send()` takes the request URL, performs
the GET, and buffers the body for `readByte()`/`available()`.

### `lora/`

`LoRaModule : Module`. The SX1276 radio only: SPI, pins, band/spreading
factor/sync word/tx power, one packet out, bytes in. `available()` reports
`LoRa.available()` with no side effect; `parsePacket()`, `receive()`,
`packetRssi()` and `packetSnr()` stay here because the radio frames packets
in hardware, so packet length and signal quality come from the physical
layer, not from a protocol we wrote.

### `lora_link/`

`LoRaLink : Transmitter`, holding a `LoRaModule&`. Unlike `EspNowUplink`,
which sits beside a `Module` rather than on top of one, `LoRaLink` has to be
stacked on `LoRaModule`: ESP-NOW's delivery confirmation is built into the
radio stack, but LoRa's ACK is our own protocol, keyed on the sent
`Payload`'s uid/seq, so it needs a layer above the raw radio to live in.
`sendPacket()` retries up to `maxRetries` times, polling for a matching
`AckPayload` until `ackTimeoutMs`. `receivePacket()` reads a `Payload`-sized
frame and replies with its own `AckPayload`. The over-the-air format is
unchanged by this split.

### `esp_now_uplink/`

`EspNowUplink : Transmitter`. ESP-NOW.

Received frames are copied inside the ESP-NOW callback into a FreeRTOS queue
(depth 4), so `receivePacket()` never runs in interrupt context. It returns the
frame length, `-1` when the queue is empty, or `-2` when the caller's buffer is
too small. `sendPacket()` blocks on a binary semaphore that the send callback
gives, so it returns only once the radio has confirmed delivery or the configured
timeout has expired.

The ESP-NOW C API takes free-function callbacks with no user pointer, so the
class keeps a `static EspNowUplink *instance` that the callbacks trampoline
through. That means one `EspNowUplink` per firmware, which is fine, since
there is one radio.

`addPeer()` registers a peer from an `EspNowPeerConfig`.

### `seq_counter/`

The reading sequence number used by the meter nodes. `seqCounterBegin()` loads
it from the `runtime` NVS namespace, and `seqNext()` increments it and writes it
back, so it survives a reboot.

### `reading_buffer/`

The substation's store and forward buffer. It keeps a ring of readings per end
node, keyed by UID, so readings are held until the gateway acknowledges them.
`readingBufferPush()`, `readingBufferPeek()`, `readingBufferPop()`,
`readingBufferCount()`.

### `ir_head/`

`IrHead : Module`, plus `setBaudRate()`, because IEC 62056-21 opens at 300 baud
and switches to the meter's offered rate for the data block. Three heads:

- `RealIrHead` is the EE team's UART to IR circuit on `Serial1`, 7E1, with
  RX/TX optionally inverted (`ir_invert`) since the circuit reads light ON as
  HIGH. Pins come from the `IR_probe_signal_testing` rig and still need
  checking against the PCB.
- `SimulatedIrHead` plays back a canned EM211 session with no wires at all.
- `TcpIrHead` carries the same bytes over `TcpBus` to `IrSim/IrSimTCP.py`, the
  IR counterpart of `ModbusSim/ModbusSimTCP.py`.

The IR node picks one with the `simulate` NVS key (0, 1, 2).

### `iec62056_21/`

`Iec6205621Reader : Reader`. IEC 62056-21 mode C: drops the head back to 300
baud, sends `/?!`, reads the identification message, ACKs the offered baud rate,
switches, then reads the data block and checks its ETX and BCC. Import (1.8.0)
and export (2.8.0) come from one session, so `get_import()` reads the block and
caches the export for `get_export()`. `get_voltage()` always fails for now.

Developed against `IrSim/` and `SimulatedIrHead`. Not yet tested against real
hardware.

## Work in progress

Neither of these is included by any built source, so neither is compiled.

### `tcp_bus/`

`TcpBus : Module`. The same raw Modbus RTU byte stream as `Sp3485`, carried over
a `WiFiClient` socket instead of RS485, matching `ModbusSim/ModbusSimTCP.py`.
This lets the RS485 node be developed with no transceiver and no meter on the
desk.

The logic is written, but it needs `TcpBusConfig`, which exists on the `RS485`
branch and not on `main`. It will not compile until that struct is merged.

## Not started

### `esp32cam/`

Two includes and a TODO. Intended to read framed messages from an ESP32-CAM over
a UART link and fill a `Payload` from an OCR'd meter face. Needs
`cam_link_protocol.h`, which is on the `ir-module` branch and is itself unwritten.
