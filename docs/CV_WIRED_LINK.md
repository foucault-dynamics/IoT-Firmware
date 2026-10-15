# CV node wired link (prototype)

The CV node can reach the ESP32-CAM two ways, and one NVS key picks which:

| `cam_link` | Link | Status |
|---|---|---|
| `0` (default) | HTTP over the C3's own Wi-Fi AP, `HttpBus` | Tested on hardware, the demo setup |
| `1` | UART over a wire, `UartCamBus` | **Prototype.** Unit tested and compiled, not yet run against a cam |

Everything above the link is shared. `CamHttpReader`, the readiness probe,
`cam_flow`, `cam_flow_exp`, `poll_ms` and the ESP-NOW uplink work the same on
both, so a demo can switch between them with two serial commands.

The wired link needs a small addition to the cam's firmware. Stock
AI-on-the-edge-device has no way to answer over its UART, so read
[The cam side](#cwl_cam) before expecting a reading.

## Switching between HTTP and the wire {#cwl_switch}

On the CV node's serial monitor:

```text
set cam_link 1        # wire
reboot
```

then press `3` at the boot menu. `set cam_link 0` and `reboot` goes back to
HTTP. Any other value also means HTTP, so a typo never strands the node.

| What changes | HTTP (`0`) | Wire (`1`) |
|---|---|---|
| Wi-Fi AP `AMR_Base` | Started, the cam joins it | Not started |
| Settings used | `ap_ssid`, `ap_pass`, `cam_host`, `cam_user`, `cam_pass` | `cam_rx`, `cam_tx`, `cam_baud` |
| First log lines | `[CV] Cam link: HTTP over the AP` | `[CV] Cam link: UART (wired)`, `[UartCamBus] UART up, RX GPIO 20, TX GPIO 21, 115200 baud` |
| ESP-NOW channel | `espnow_chan`, shared with the AP | `espnow_chan` |

Settings for the wire, all optional:

| Key | Default | Meaning |
|---|---|---|
| `cam_link` | `0` | `1` for the wire |
| `cam_rx` | `20` | C3 GPIO wired to the cam's TX |
| `cam_tx` | `21` | C3 GPIO wired to the cam's RX |
| `cam_baud` | `115200` | Line rate, 8N1. Must match the cam |

GPIO 20 and 21 are the pins the SuperMini labels RX and TX. They are free
because the C3's `Serial` runs over its own USB, and the CV node drives them
with `Serial1`. Any other free GPIO works through `cam_rx` and `cam_tx`, but
avoid 2, 8 and 9, which are strapping pins.

## Wiring {#cwl_wiring}

A UART link needs three wires, crossed over, plus power for the cam:

| C3 SuperMini | ESP32-CAM | Why |
|---|---|---|
| GPIO 20 (RX, `cam_rx`) | U0TXD, GPIO 1 | Cam talks, C3 listens |
| GPIO 21 (TX, `cam_tx`) | U0RXD, GPIO 3 | C3 asks, cam listens |
| GND | GND | Shared reference. Without it the link reads garbage |
| 5V | 5V | Power, if the cam is powered from the C3's USB |

Both boards use 3.3 V logic, so no level shifting is needed.

Things to check against the bench photo (`IMG_6347.jpg`). It shows the C3 on
a breadboard and the cam's adapter board joined to it by two wires, red and
yellow, running to the breadboard's right hand side. The photo does not show
which C3 pins they reach. Two wires are enough for power (5V, GND) or for one
direction of data, but the request and reply need TX, RX and a shared GND.
Confirm the wiring against the table, or set `cam_rx` and `cam_tx` to match
the custom board.

- **Take the cam off the ESP32-CAM-MB programmer board** for the wired link.
  The MB board's CH340 drives the cam's U0RXD too, and two outputs on one pin
  fight. Keep the MB board for flashing and for testing the cam on its own.
- **The cam's U0TXD is also its log console.** That is fine: the C3 only reads
  what is inside a reply frame. If the custom board wires a different cam
  UART, set its pins on the cam side and nothing changes here.
- **Power.** The ESP32-CAM draws current peaks well above what the C3's 3.3 V
  regulator gives, so feed it 5 V.

## Protocol {#cwl_protocol}

One request, one reply, 115200 8N1 by default.

1. The C3 sends one ASCII line: `GET /json` then `\n`.
2. The cam answers within 2 s with STX (`0x02`), the exact body its HTTP
   server sends for `/json`, then ETX (`0x03`).

```text
C3  -> cam   47 45 54 20 2F 6A 73 6F 6E 0A          "GET /json\n"
cam -> C3    02 {"main":{"value":"531.82235",...}} 03
```

The C3 side rules, in `UartCamBus` and `CamUartFrame`:

- Anything already waiting is dropped before the request is sent.
- Bytes before STX are skipped, so log lines around the reply do no harm.
- A second STX restarts the frame, so a cut off reply followed by a full one
  still works.
- No ETX within 2 s (`UartCamConfig::timeoutMs`), or a body over 2048 bytes
  (`CAM_UART_MAX_BODY`), fails that request. The node tries again on its next
  poll.
- The body goes to `CamHttpReader` unchanged, so it is checked exactly as an
  HTTP reply is: valid JSON, the flow present, no cam error, a value.

The request line carries the path from the URL the reader builds, so a future
request for another page (`GET /value?all=true`) needs no protocol change.

## The cam side {#cwl_cam}

Stock AI-on-the-edge-device never answers on its UART, so the wired link
needs a small addition to the cam's firmware, the same branch the cam already
runs. What it must do:

1. Read its UART (UART0 at 115200, or whichever UART the custom board wires),
   collecting bytes into a line until `\n`. Ignore `\r`.
2. On the line `GET /json`, build the same JSON the `/json` HTTP handler
   returns and write `0x02`, the JSON, `0x03`.
3. On any other `GET` line, write `0x02 0x03`, an empty reply.
4. Never write `0x02` or `0x03` anywhere else. ESP-IDF log text never
   contains them.

A sketch for ESP-IDF, to adapt rather than paste. The function that builds the
JSON differs between branches: use whatever your `/json` HTTP handler calls,
upstream that is `ClassFlowControll::getJSON()` in `server_main.cpp`.

```cpp
#include "driver/uart.h"

static void wiredJsonTask(void *) {
  const uart_port_t port = UART_NUM_0;
  uart_driver_install(port, 1024, 0, 0, nullptr, 0);  // UART0 is already configured as the console
  std::string line;
  uint8_t b;
  while (true) {
    if (uart_read_bytes(port, &b, 1, portMAX_DELAY) != 1) continue;
    if (b == '\r') continue;
    if (b != '\n') { if (line.size() < 64) line += (char)b; continue; }
    std::string body = (line == "GET /json") ? flowctrl.getJSON() : "";
    std::string frame = "\x02" + body + "\x03";
    uart_write_bytes(port, frame.data(), frame.size());  // one write keeps log lines out of the frame
    line.clear();
  }
}

// After the flow is set up:
// xTaskCreate(wiredJsonTask, "wiredJson", 4096, nullptr, 5, nullptr);
```

If a log line still lands inside a frame, the JSON fails to parse on the C3,
which logs `JSON parse failed` and tries again on its next poll. Lowering the
cam's console log level, or moving the link to a UART of its own, stops that.

## Bring-up, one step at a time {#cwl_bringup}

Each step can be done without the hardware the later steps need.

| Step | Needs | Run | Expect |
|---|---|---|---|
| 1 Unit tests | Nothing | `pio test -e test_c3 --without-uploading -f test_qemu_cam_uart` | 13 tests pass. Also runs in CI |
| 2 C3 UART, no wires | The C3 on USB | `pio test -e end_node -f test_board_cam_uart` | 5 tests pass. UART1 in internal loopback, a task plays the cam |
| 3 C3 against a fake cam | The C3, a 3.3 V USB to serial adapter, 3 jumpers | `python CamSim/CamSimSerial.py /dev/ttyUSB0` on the laptop, `set cam_link 1` on the C3 | `[CV] Cam answered with every configured flow, node ready.`, then `[CV] import=530.071 ...` |
| 4 Cam on its own | The cam with the wired JSON addition, on its MB board | `python CamSim/cam_uart_poll.py /dev/ttyUSB0` | `#1: 374 byte frame in .. ms: main='531.82235' (no error)` |
| 5 Together | Both boards wired | `set cam_link 1`, `reboot`, `3` | As step 3, with the cam's real values |

For step 3, wire the adapter's TX to C3 GPIO 20, the adapter's RX to C3 GPIO
21, and GND to GND. `CamSimSerial.py --help` lists ways to make it misbehave
(silent, missing flow, no value, no ETX) to check each error path. For step 4
the MB board's CH340 already reaches the cam's UART, so no extra wiring.
`CamSim/README.md` has the details of both tools.

## Troubleshooting {#cwl_trouble}

| Symptom on the C3 | Likely cause |
|---|---|
| `[UartCamBus] Bad config: baud is 0 or RX and TX are the same pin.` | `cam_rx`, `cam_tx` or `cam_baud` set wrong. `clear` resets them |
| `[UartCamBus] No reply frame within 2000 ms.` every 5 s | TX and RX not crossed, GND missing, `cam_baud` different from the cam, the cam still on its MB board, or the cam firmware without the wired JSON addition |
| `[Cam HTTP Reader] Empty response body` | The cam answered with `0x02 0x03`: it did not recognise the path, or has no JSON yet |
| `[Cam HTTP Reader] JSON parse failed` now and then | A cam log line landed inside the frame. See the end of the cam side section |
| `[UartCamBus] Reply longer than 2048 bytes, dropped.` | A frame never closed with ETX and the log text after it filled the buffer, or the cam sent more than `/json` |
| `Flow "main" not present in response.` | The link works. `cam_flow` does not match the cam's number name, same as over HTTP |
| `[CV] Cam link: HTTP over the AP` after `set cam_link 1` | Not rebooted yet, or the value was sent quoted (`"1"` stores a string, which is never read as a number) |

## Known limits of the prototype {#cwl_limits}

- Not yet run against a real cam, and the cam side is a sketch.
- The reply has no checksum. JSON parsing catches corrupt replies in practice,
  and the node retries every poll.
- The C3 blocks for up to 2 s waiting for a reply, the same as an HTTP request
  blocks for up to 5 s.
- The wire pins are only read at boot, like every other setting.
