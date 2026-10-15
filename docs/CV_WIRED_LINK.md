# CV node wired link (prototype)

The CV node can reach the ESP32-CAM two ways, and one NVS key picks which:

| `cam_link` | Link | Status |
|---|---|---|
| `0` (default) | HTTP over the C3's own Wi-Fi AP, `HttpBus` | Tested on hardware, the demo setup |
| `1` | UART over a wire, `UartCamBus` | **Prototype.** Each side tested on hardware on its own, not yet wired together (see [Bring-up](#cwl_bringup)) |

Everything above the link is shared. `CamHttpReader`, the readiness probe,
`cam_flow`, `cam_flow_exp`, `poll_ms` and the ESP-NOW uplink work the same on
both, so a demo can switch between them with two serial commands.

The wired link needs a small addition to the cam's firmware, on the `wired-json-uart` branch of the cam fork. Stock
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

**Confirm every wire against the table before powering either board.** The
bench wiring has never been checked against it. What was there last was two
wires between the C3's breadboard and the cam's adapter board, which is enough
for power (5V and GND), or for one direction of data, but not for a request
and a reply: those need TX, RX and a shared GND, three wires besides power.
Trace each wire end to end, then either rewire to the table or set `cam_rx`
and `cam_tx` to the pins the board actually uses.

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

Stock AI-on-the-edge-device never answers on its UART, so the cam runs a small
addition: branch `wired-json-uart` of
[foucault-dynamics/AI-on-the-edge-FD](https://github.com/foucault-dynamics/AI-on-the-edge-FD),
described in that repo's `WIRED_JSON.md`. It has run on a cam on its own (step
4 of [Bring-up](#cwl_bringup)), but not yet with a node on the other end of the
wire.

**Check what the cam is actually flashed with before testing.** The branch has
to include the `getJSON()` null guard (`Handle JSON retrieval before flow
initialization`, cherry-picked onto `wired-json-uart` on 2026-10-15). Without
it the cam dereferences a null pointer when `/json` is asked for after the
flow task exists but before the flow is initialized, and the C3 probes every
5 s from cold boot, so it lands in that window on every start. `git log
--oneline -3` on the cam's branch should show the guard above the wired JSON
commit.

What it does:

1. Installs the UART0 driver (the console UART, 115200 8N1) and reads bytes
   into a line until `\n`, ignoring `\r`.
2. On `GET /json`, writes `0x02`, the JSON from `flowctrl.getJSON()` (exactly
   what the `/json` web handler sends), then `0x03`, in one write.
3. On any other `GET` line, or before the flow has started, writes `0x02 0x03`,
   an empty reply.
4. Ignores anything that is not a `GET` line.

It is switched on by `-D ENABLE_WIRED_JSON` in the cam's `platformio.ini`, and
every line of it sits inside `#ifdef ENABLE_WIRED_JSON`. GPIO 1 and 3 then
belong to the link, so leave `IO1` and `IO3` off in the `[GPIO]` section of the
cam's `config.ini`.

If a log line still lands inside a frame, the JSON fails to parse on the C3,
which logs `JSON parse failed` and tries again on its next poll. Lowering the
cam's console log level, or moving the link to a UART of its own, stops that.

**Seeing the cam's own log once it is on the wire.** With `cam_link 1` the C3
never starts `AMR_Base`, so a cam configured for that AP has no network, and
its console goes to the C3 instead of a laptop. The cam is built with
`ENABLE_SOFTAP`, so once the join fails it raises an AP of its own: join that
from a laptop and the web UI's System, Log Viewer page shows the cam's log
without touching the wire. Without it the cam is debugged blind.

## Bring-up, one step at a time {#cwl_bringup}

Each step can be done without the hardware the later steps need. Status on
2026-10-15: steps 1, 2 and 4 pass, 3 and 5 still to do.

Before step 5, confirm three things: the wiring against [Wiring](#cwl_wiring);
the cam's branch, including the null guard (see [The cam side](#cwl_cam)); and
the C3's own firmware. `UartCamBus` only arrived in `d99eb2a` on 2026-10-15,
after the build the bench C3 has been running since 2026-10-14, so the C3 needs
reflashing from the branch tip or `set cam_link 1` changes nothing:

```sh
pio run -e end_node -t upload
```

- Step 2: 5 of 5 on a C3 SuperMini.
- Step 4: the cam, flashed from `wired-json-uart`, answered every
  `GET /json` with a 189 byte frame in about 50 ms, values matching its demo
  cycle (`main='530.07429'`, then `main='530.12246'`).

| Step | Needs | Run | Expect |
|---|---|---|---|
| 1 Unit tests | Nothing | `pio test -e test_c3 --without-uploading -f test_qemu_cam_uart` | 13 tests pass. Also runs in CI |
| 2 C3 UART, no wires | The C3 on USB | `pio test -e end_node -f test_board_cam_uart` | 5 tests pass. UART1 in internal loopback, a task plays the cam |
| 3 C3 against a fake cam | The C3, a 3.3 V USB to serial adapter, 3 jumpers | `python CamSim/CamSimSerial.py /dev/ttyUSB0` on the laptop, `set cam_link 1` on the C3 | `[CV] Cam answered with every configured flow, node ready.`, then `[CV] import=530.071 ...` |
| 4 Cam on its own | The cam flashed from `wired-json-uart`, on its MB board | `python CamSim/cam_uart_poll.py /dev/ttyUSB0` | `#1: 374 byte frame in .. ms: main='531.82235' (no error)` |
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
| The cam resets, or goes quiet, soon after the C3 starts probing | Cam firmware without the `getJSON()` null guard. See [The cam side](#cwl_cam) |
| `[CV] Cam link: HTTP over the AP` after `set cam_link 1` | Not rebooted yet, or the value was sent quoted (`"1"` stores a string, which is never read as a number) |

## Serial port gotchas {#cwl_gotchas}

Found during bring-up, and worth knowing before scripting either board:

- **Open ports with DTR and RTS released** (`dtr = False`, `rts = False`
  before `open()` in pyserial). On the ESP32-CAM-MB, DTR pulls the cam's IO0
  low, so any reset while the port is held lands in download mode, and IO0 is
  also the camera's clock pin. On the C3's own USB, DTR forces GPIO 9 low
  during a reset, with the same result. `cam_uart_poll.py` does this.
- **The ESP32-CAM-MB in use cannot reset the cam by itself.** Its RTS line
  does not reach EN, so esptool cannot enter the bootloader. To flash: hold
  IO0, tap RST on the cam, release IO0, flash, then tap RST again with no
  program holding DTR.

## Known limits of the prototype {#cwl_limits}

- The C3 and the cam have each been tested on hardware, but never wired to
  each other.
- The reply has no checksum. JSON parsing catches corrupt replies in practice,
  and the node retries every poll.
- The C3 blocks for up to 2 s waiting for a reply, the same as an HTTP request
  blocks for up to 5 s.
- The wire pins are only read at boot, like every other setting.
