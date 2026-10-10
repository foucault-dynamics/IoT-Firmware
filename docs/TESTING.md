# Testing

The firmware's unit tests run inside an emulated ESP32-C3, using Espressif's
fork of QEMU and the real Arduino core. The
only thing QEMU cannot run is the radio, so Wi-Fi and ESP-NOW tests need a real
C3 run.

## Quick start

```sh
tools/setup_qemu.sh                          # once, downloads QEMU into ~/.kaizen/qemu
export ESP_QEMU=~/.kaizen/qemu/esp-develop-9.2.2-20260417/qemu/bin/qemu-system-riscv32
pio test -e test_c3 --without-uploading     # every QEMU suite
```

Supported in  macOS on Apple Silicon and Linux x86_64.

On macOS QEMU needs these Homebrew libraries:

```sh
brew install libgcrypt glib pixman sdl2 libslirp
```

### Useful variations

| Command | Does |
|---|---|
| `pio test -e test_c3 --without-uploading -f test_qemu_nvs` | Runs one suite |
| `pio test -e test_c3 --without-uploading -v` | Also shows the firmware's serial output, including crash dumps |
| `pio test -e end_node -f test_board_radio` | Runs the real board suite on a connected C3 |
| `pio test -e end_node --without-uploading --without-testing` | Only builds the board suites, no board needed |

## Layout

```text
test/
├── support/
│   └── fake_bus.h                FakeBus, the scripted bus for driver tests
├── test_qemu_reading_buffer/     run in QEMU by test_c3
├── test_qemu_modbus_frame/
├── test_qemu_modbus_driver/
├── test_qemu_dlms_frame/
├── test_qemu_dlms_driver/
├── test_qemu_iec62056/
├── test_qemu_nvs/
├── test_qemu_payload/
├── test_qemu_cam_http/
├── test_qemu_payload_json/
└── test_board_radio/             needs a real C3, run by end_node
```

Every folder starting with `test_` is a suite: its own firmware image with its
own `setup()`. The prefix decides where it runs.

| Prefix | Environment | Runs in |
|---|---|---|
| `test_qemu_*` | `test_c3` | QEMU, locally and in CI |
| `test_board_*` | `end_node` | A real ESP32-C3 over USB, never in CI |

Each environment's `test_filter` in `platformio.ini` picks its prefix, so a new
suite only needs the right folder name.

## Suites

| Suite | Covers |
|---|---|
| `test_qemu_reading_buffer` | The substation's store and forward buffer: FIFO order, overflow, the slot limit, node rotation, and peek without pop |
| `test_qemu_modbus_frame` | Modbus CRC16 vectors, request frames, and the T3.5 gap for each baud rate and frame format |
| `test_qemu_modbus_driver` | Decoding both register formats, every way a response is rejected, the 500 ms timeout, replies split across reads, and recovery after a failure |
| `test_qemu_dlms_frame` | HDLC FCS, addresses, building and parsing frames, the AARQ and AARE, GET requests and responses, A-XDR numbers, and scaler_unit |
| `test_qemu_dlms_driver` | Reading HDLC frames off the bus, the 1000 ms timeout, SNRM and DISC, I-frame sequencing, and whole sessions through the getters, including DISC after a failure |
| `test_qemu_iec62056` | The baud rate ID table, OBIS parsing, a full optical port session, and handshake failures |
| `test_qemu_nvs` | NVS read helpers, the defaults and NVS overrides of every node's config loader, and the sequence counter resuming from NVS |
| `test_qemu_payload` | The over the air packets: size, field offsets and byte image of Payload and AckPayload, and the UID helpers |
| `test_qemu_cam_http` | The CV node's CamHttpReader: the request URL, a good read, every way the cam's JSON is rejected, and the unsupported registers |
| `test_qemu_payload_json` | The gateway's MQTT JSON: the exact output for a known reading, `ts` before and after NTP sync, the `+10:00` offset, and buffer sizes |
| `test_board_radio` | Wi-Fi station start, channel conflicts, ESP-NOW init, adding a peer, and sends that fail: no ACK, unknown peer, bad length |

The DLMS suites check the reader against frames written for the tests. To check
it against an independent implementation, run it against `DlmsSim/`, a Gurux
based meter, over TCP (`reader = 6`) or RS485 (`reader = 5`). See
`DlmsSim/README.md`.

## How a QEMU run works

`pio test -e test_c3 --without-uploading` builds each suite, then, instead of
flashing a board, runs the command in `test_testing_command`:

1. `tools/qemu_test.py` merges the bootloader, partition table, `boot_app0.bin`
   and the test firmware into one 4 MB flash image, using PlatformIO's bundled
   esptool.
2. It boots that image with `$ESP_QEMU -machine esp32c3` and streams the serial
   output back to PlatformIO, which parses Unity's results from it.
3. It stops QEMU as soon as Unity prints `OK` or `FAIL`. If neither appears
   within 60 seconds, because the firmware crashed or hung, it kills QEMU and
   fails the run. 

Every run starts from a freshly merged image, so NVS is always empty at boot.

`test_c3` extends `end_node` and changes one flag:
`ARDUINO_USB_CDC_ON_BOOT=0`. QEMU does not emulate the C3's USB port, so
`Serial` has to go to the UART instead, or nothing prints after the bootloader.

## Writing a test

### Suite skeleton

```cpp
#include <Arduino.h>
#include <unity.h>

#include "reading_buffer.h"

void setUp() {}
void tearDown() {}

void test_something() {
  TEST_ASSERT_EQUAL_INT(1, 1);
}

void setup() {
  delay(500);
  UNITY_BEGIN();
  RUN_TEST(test_something);
  UNITY_END();
}

void loop() {}
```

Save it as `test/test_qemu_<name>/test_main.cpp`. Libraries are found from
their `#include`, as in the firmware. `src/` is never built into tests, so the
suite's `setup()` does not clash with the firmware entry files.

`test/` is outside the Doxygen input, but test code follows
[`COMMENTING.md`](COMMENTING.md).

### Reaching private functions

Prefer testing through the public interface. When a private helper is worth
testing on its own, such as a CRC or a parser, the class declares a test
friend, only in test builds:

```cpp
class ModbusRtuReader : public Reader {
 private:
#ifdef PIO_UNIT_TESTING
  friend class ModbusRtuReaderTest;
#endif
```

PlatformIO defines `PIO_UNIT_TESTING` only during `pio test`, so release builds
are unchanged. The suite then defines that class with static functions that
forward to the private ones. See `test_qemu_modbus_frame` and
`test_qemu_iec62056`. `ModbusRtuReader` and `Iec6205621Reader` for examples.

### Faking a bus

`test/support/fake_bus.h` provides `FakeBus`, a `Module` (an `IrHead`, so it
also has `setBaudRate()`) that stands in for the meter:

```cpp
#include "../support/fake_bus.h"

FakeBus bus;
bus.queueReply({0x01, 0x03, 0x04, /* ... */});   // answer to the 1st send()
bus.queueSilence();                              // the 2nd send() gets nothing
bus.queueChunks({{first, 0}, {rest, 1000}});     // 3rd: split, 1000 us gap

reader.init(bus);
// ... call the reader ...
bus.sent;       // every frame the reader sent
bus.baudRates;  // every baud rate change
```

Each `send()` starts the next queued reply. Timing is real, so a gap or a
timeout in a test takes that long.

### Real board suites

Name the folder `test/test_board_<name>`. These run on whatever C3 is connected
over USB, with the normal `end_node` build flags. Keep them for what QEMU cannot
do, such as the radio. Anything else belongs in a QEMU suite, because only
those run in CI.

## CI

`.github/workflows/firmware.yml` runs on every push to `main` and every pull
request:

| Job | Does |
|---|---|
| `build (end_node)`, `build (lilygo_lora)` | `pio run` for each firmware environment |
| `test` | Installs QEMU with `tools/setup_qemu.sh` and runs `pio test -e test_c3 --without-uploading` |

PlatformIO packages and the QEMU download are cached between runs. To change
the QEMU version, edit `QEMU_TAG` and `QEMU_VERSION` in `tools/setup_qemu.sh`.
The cache is keyed on that file, so CI downloads the new release.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `ESP_QEMU is not set` | Run `tools/setup_qemu.sh` and export the line it prints |
| `unsupported platform` from `setup_qemu.sh` | Only macOS arm64 and Linux x86_64 have QEMU releases. Use CI, or a Linux x86_64 machine |
| `no Unity result after 60 s` | The firmware crashed or hung. Rerun with `-v` to see the crash dump |
| Suite shows `ERRORED` with no test results | Same as above, rerun with `-v` |
| QEMU fails to start on macOS with a missing `.dylib` | Install the Homebrew libraries listed in the quick start |
| A board suite prints nothing | Give USB serial time to enumerate. Board suites wait 2 s in `setup()` |
