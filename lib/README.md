# lib/

Project-private libraries. PlatformIO compiles each subdirectory into its own
static library and links it into the firmware. The Library Dependency Finder
scans the source files for `#include`s and pulls in only what is actually
reachable, so a subdirectory that nothing includes is never compiled. That is
why the work-in-progress modules below do not break the build.

Each library lives directly in `lib/<name>/` as a flat `.h` plus `.cpp` pair.
More on the LDF: https://docs.platformio.org/page/librarymanager/ldf.html

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
`init()`, `send(data, len)`, `readByte()` (returns the byte, or `-1` if none is
waiting), `available()`.

### `reader/`

`Reader`, the base class for a meter protocol sitting on top of a `Module&`.
`init(Module&, const void *config)`, `get_import()`, `get_export()`,
`get_voltage()`, all writing through a `float*` and returning `EXIT_SUCCESS` or
`EXIT_FAILURE`.

The config is passed as `const void *` and each reader casts it to the struct it
expects. That keeps every protocol's fields out of the shared base interface, at
the cost of the cast being unchecked, so a reader and its config must be paired
correctly by the caller in `src/nodes/`.

### `transmitter/`

`Transmitter`, the base class for anything that moves a whole packet to a peer
address. `init()`, `sendPacket(address, buf, len)`,
`receivePacket(address, buf, bufLen)`. The address is `const void *` for the same
reason the reader config is: an ESP-NOW address is a 6-byte MAC, a LoRa address
will be something else.

## Configuration

### `node_config/`

One plain struct per transport and per protocol (`Rs485Config`, `EspNowConfig`,
`EspNowPeerConfig`, `ModbusRtuConfig`), plus the `ReaderType`, `MeterModel`, and
`RegisterFormat` enums.

`node_config.cpp` holds `loadReaderType()`, `loadModbusRtuConfig()`, and
`loadEspNowConfig()`. These are hardcoded on purpose. They are the seam where
upstream configuration selection will plug in later, so their bodies can be
replaced without touching a single call site. `loadModbusRtuConfig()` also
carries the `METER_MODELS` table mapping a `MeterModel` to its register
addresses, currently a linear scan over two rows.

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

### `wifi/`

`Wifi : Transmitter`. ESP-NOW.

Received frames are copied inside the ESP-NOW callback into a FreeRTOS queue
(depth 4), so `receivePacket()` never runs in interrupt context. It returns the
frame length, `-1` when the queue is empty, or `-2` when the caller's buffer is
too small. `sendPacket()` blocks on a binary semaphore that the send callback
gives, so it returns only once the radio has confirmed delivery or the configured
timeout has expired.

The ESP-NOW C API takes free-function callbacks with no user pointer, so the
class keeps a `static Wifi *instance` that the callbacks trampoline through. That
means one `Wifi` per firmware, which is fine, since there is one radio.

`addPeer()` registers a peer from an `EspNowPeerConfig`.

## Work in progress

Neither of these is included by any built source, so neither is compiled.

### `tcp_bus/`

`TcpBus : Module`. The same raw Modbus RTU byte stream as `Sp3485`, carried over
a `WiFiClient` socket instead of RS485, matching `ModbusSim/ModbusSimTCP.py`.
This lets the RS485 node be developed with no transceiver and no meter on the
desk.

The logic is written, but it needs `TcpBusConfig`, which exists on the `RS485`
branch and not on `main`. It will not compile until that struct is merged.

### `substation/`

`Substation : Transmitter`, intended to wrap the LoRa link the way `Wifi` wraps
ESP-NOW, so the substation node stops calling the `LoRa` library directly.

Skeleton only. `substation.h` currently has a syntax error
(`class Substation :: public Transmitter`), the three method bodies are empty,
and the signatures do not match the `Transmitter` base.

## Not started

### `ir_head/`

Header only, and it declares nothing. Intended to be an `IrHead : Module` for the
optical probe on the meter's front panel. Many meters expose the same register
map over the optical port as over RS485, so `ModbusRtuReader` should run on it
unchanged once it exists. Needs `pin_config.h`, which is on the `ir-module`
branch, along with a working IR stack (`lib/iec62056_21/`, real and simulated
heads) that should be ported here.

### `esp32cam/`

Two includes and a TODO. Intended to read framed messages from an ESP32-CAM over
a UART link and fill a `Payload` from an OCR'd meter face. Needs
`cam_link_protocol.h`, which is on the `ir-module` branch and is itself unwritten.
