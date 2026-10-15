# CamSim

Tools for the CV node's wired cam link (`cam_link 1`), the counterpart of
`ModbusSim/`, `IrSim/` and `DlmsSim/`. The link and its protocol are described
in [docs/CV_WIRED_LINK.md](../docs/CV_WIRED_LINK.md).

| File | Plays | Use it to test |
|---|---|---|
| `CamSimSerial.py` | The cam | The C3's wired path, before the cam side exists |
| `cam_uart_poll.py` | The C3 | The cam's wired path, before it is wired to a C3 |
| `cam_protocol.py` | | Framing and the demo values, shared by both |

Both need Python 3 and pyserial:

```sh
cd CamSim
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install pyserial
```

## CamSimSerial.py, a fake cam

Answers `GET /json` with a framed reply shaped like AI-on-the-edge-device's
`/json`, stepping through the true values of the cam's 12 demo images, one per
minute like the real demo.

Wire a 3.3 V USB to serial adapter to the C3:

| Adapter | C3 SuperMini |
|---|---|
| TX | GPIO 20 (`cam_rx`) |
| RX | GPIO 21 (`cam_tx`) |
| GND | GND |

Then:

```sh
python3 CamSimSerial.py /dev/ttyUSB0                   # COM5 on Windows
python3 CamSimSerial.py /dev/ttyUSB0 --export export   # also serve an export flow
python3 CamSimSerial.py /dev/ttyUSB0 --noise           # print cam-style log lines between replies
python3 CamSimSerial.py /dev/ttyUSB0 --step 5          # a new value every 5 s
```

On the C3, `set cam_link 1`, `reboot`, press `3`, and expect:

```text
[CV] Cam link: UART (wired)
[UartCamBus] UART up, RX GPIO 20, TX GPIO 21, 115200 baud
[CV] Cam answered with every configured flow, node ready.
[CV] import=530.071 export=0.000 seq=...
```

With `--export export` on the sim, `set cam_flow_exp "export"` on the C3 sends
export too.

`--fail` makes it misbehave, to check each error path on the C3:

| `--fail` | The sim | The C3 logs |
|---|---|---|
| `silent` | Never answers | `[UartCamBus] No reply frame within 2000 ms.` |
| `missing` | Replies without the flow | `Flow "main" not present in response.`, never ready |
| `novalue` | Empty value, as just after the cam boots | `Flow "main" has no value yet.` |
| `noetx` | Cuts the frame off before ETX | `[UartCamBus] No reply frame within 2000 ms.` |

## cam_uart_poll.py, a fake C3

Sends `GET /json` the way the C3 does and prints what comes back. Point it at
the cam once it runs the `wired-json-uart` branch of the cam fork
(AI-on-the-edge-FD, see its `WIRED_JSON.md`). With the cam on its
ESP32-CAM-MB board, the MB board's CH340 already reaches the cam's UART, so no
extra wiring:

```sh
python3 cam_uart_poll.py /dev/ttyUSB0
python3 cam_uart_poll.py /dev/ttyUSB0 --count 0 --every 30   # keep polling
python3 cam_uart_poll.py /dev/ttyUSB0 --show-noise           # print the log text it skipped
```

```text
#1: 374 byte frame in 66 ms: main='531.82235' (no error)
```

It waits `--settle` seconds (default 5) after opening the port before the
first request, because the MB board's CH340 replays stale bytes for a few
seconds after the port opens (see `docs/hw-tests/2026-10-13-cv-node/README.md`,
finding 3).

## Without any adapter

The two tools also run against each other through a virtual serial pair, which
is how they were checked:

```sh
socat -d pty,raw,echo=0,link=/tmp/camA pty,raw,echo=0,link=/tmp/camB &
python3 CamSimSerial.py /tmp/camA &
python3 cam_uart_poll.py /tmp/camB --settle 0.5
```
