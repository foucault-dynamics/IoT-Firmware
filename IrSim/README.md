# IrSim

A fake IR meter in Python, the IR version of `ModbusSim/`. It stands in for a
meter's optical port so the IR node (`src/nodes/ir_node.cpp`,
`lib/protocols/iec62056_21/`) can be tested with no meter and no IR circuit on the desk.

It speaks IEC 62056-21 mode C, like a real optical-port meter:

```text
reader -> /?!\r\n                      request, at 300 baud 7E1
meter  -> /EMH5EM211\r\n               identification; the '5' offers 9600 baud
reader -> <ACK>050\r\n                 acknowledge, switch to 9600
meter  -> <STX>...data...!\r\n<ETX><BCC>   data block, at 9600
```

Import (`1-0:1.8.0`) and export (`1-0:2.8.0`) kWh go up on every read, so two
reads never return the same thing and a stale reading is obvious.

| File | What it is |
|---|---|
| `ir_meter.py` | The fake meter itself: protocol logic shared by both servers. |
| `IrSimTCP.py` | Fake meter over TCP on `0.0.0.0:5021`. The firmware reaches it with `TcpIrHead` (`simulate 2`). No dependencies. |
| `IrSimSerial.py` | Fake meter on a serial port. Real 300 baud 7E1, real baud switch. Closest to the real thing. Needs `pyserial`. |
| `ir_poll_test.py` | Reads a meter exactly like the firmware does, from the laptop. Use it to check a server before pointing firmware at it. |
| `native/` | Builds the firmware's real `Iec6205621Reader` for the laptop and runs it against fake meters. `sh IrSim/native/run.sh` |

`--no-frame` on either server drops STX/ETX/BCC so the data block matches
`SimulatedIrHead` byte for byte.

## Ways to test, from no hardware up

| | You need | Head (`simulate`) | Tests |
|---|---|---|---|
| 1 | Laptop only | none | The fake meter and the protocol, from Python |
| 2 | ESP32 only | `SimulatedIrHead` (`1`) | The firmware's parser, with canned replies |
| 3 | ESP32 + laptop on WiFi | `TcpIrHead` (`2`) | The whole IR node against a live, changing meter. Seiji's ModbusSim way. |
| 4 | ESP32 + USB-to-serial adapter | `RealIrHead` (`0`) | Everything in 3, plus the real UART, 7E1 and the baud switch |
| 5 | The PCB + a real meter | `RealIrHead` (`0`) | The IR circuit itself |

### Setup

Windows uses `py`; on macOS/Linux use `python3`.

```bash
cd IrSim
py -m venv .venv
.venv\Scripts\activate          # macOS/Linux: source .venv/bin/activate
py -m pip install pyserial      # only needed for 4 and 5
```

### 1. Laptop only

Two terminals:

```bash
py IrSimTCP.py
```

```bash
py ir_poll_test.py
```

Expect `3/3 reads ok`, with import and export going up each read. The server
terminal shows every byte both ways.

That tests the fake meter. To test the firmware's own reader code with no
board, from the repo root (needs `g++`, e.g. from MSYS2):

```bash
sh IrSim/native/run.sh
```

It runs `Iec6205621Reader` three polls each against `SimulatedIrHead`, a
meter that behaves like a real UART one (framed block, drops back to 300
baud), and the same meter with IR echo. Expect `3/3 ok` for all three.

### 2. ESP32 only

Nothing new. `SimulatedIrHead` is the default (`simulate 1`). Flash the
`unified` environment, open the serial monitor, press `2` for IR node.

### 3. ESP32 + laptop on WiFi

1. Flash `unified`, open the serial monitor, then set the mode and reboot:

   ```text
   set simulate 2
   reboot
   ```

   Press `2` for IR node after the reboot.

2. The ESP32 hosts a WiFi network (`ap_ssid`, default `SECRET_AP_SSID` from
   `secrets.h`). Join it from the laptop. The first device to join gets
   `192.168.4.2`, which is the default `tcp_host`. Check with `ipconfig`; if
   yours is different, `set tcp_host "192.168.4.3"` and reboot.

3. Run `py IrSimTCP.py`. Windows Firewall may ask about Python the first time:
   allow it on private networks, or the ESP32 can't connect.

4. Each poll (`poll_ms`, default 60 s; `set poll_ms 5000` for faster) shows the
   handshake in the server terminal and `[IR] import=... export=...` in the
   serial monitor.

### 4. ESP32 + USB-to-serial adapter

The IR circuit is a UART that uses light instead of a wire. Skip the light and
plug the wire straight in, and `RealIrHead` runs exactly as it will on the PCB.

Use a **3.3 V** USB-to-serial adapter (CP2102, CH340, FT232 set to 3.3 V). A 5 V
one can damage the ESP32.

| Adapter | ESP32-C3 |
|---|---|
| TX | GPIO 20 (`IR_RX_PIN`) |
| RX | GPIO 21 (`IR_TX_PIN`) |
| GND | GND |

TX goes to RX, crossed over. Pins are in `lib/config/nvs_config/ir_loader.cpp`, taken from
the EE team's `IR_probe_signal_testing` rig. Unplug the IR circuit from those
pins first.

1. Find the adapter's port: Device Manager > Ports (COM & LPT), e.g. `COM6`.
2. `py IrSimSerial.py COM6`
3. On the ESP32: `set simulate 0`, `set ir_invert 0`, `reboot`, press `2`.

`ir_invert 0` matters: the IR circuit needs the UART inverted (light ON reads
HIGH, but IEC 62056-21 sends a 0 bit as light ON), and a plain adapter doesn't.
Set it back to `1` before using the IR circuit.

The server log shows the request and identification arriving at 300 baud, then
the data block at 9600.

### 5. The PCB

Same as 4 but against a real meter, with `RealIrHead` on the PCB's IR pins
and `ir_invert 1`. Check the PCB's pins against `IR_RX_PIN` / `IR_TX_PIN`
first; they come from the test rig, not the PCB.
To check the PCB's readings, read the same meter from the laptop with an
optical probe and `py ir_poll_test.py --serial COM6`. That uses the same
handshake as the firmware, so the two should agree.

## What to look for

Every poll should succeed, not just the first. These were bugs this fake meter
was built to catch, now fixed in `Iec6205621Reader` and `SimulatedIrHead`:

- `SimulatedIrHead` got stuck after one read and never answered again.
- The reader stayed at 9600 baud after a read, so the next request went out
  too fast for a meter that had gone back to 300. It now resets to 300 first.
- The ETX and BCC after the data block were left in the receive buffer and
  broke the next read. They're now read and the BCC is checked.

If a later poll fails, the serial monitor says which step: no answer, a
malformed identification, a missing ETX, or a BCC mismatch.
