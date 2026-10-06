# IrSim

A fake IR meter in Python, the IR version of `ModbusSim/`. It stands in for a
meter's optical port so the IR node (`src/nodes/ir_node.cpp`,
`lib/iec62056_21/`) can be tested with no meter and no IR circuit on the desk.

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
| TX | GPIO 4 (`IR_RX_PIN`) |
| RX | GPIO 5 (`IR_TX_PIN`) |
| GND | GND |

TX goes to RX, crossed over. Pins are in `src/nodes/nvs_config.cpp`.

1. Find the adapter's port: Device Manager > Ports (COM & LPT), e.g. `COM6`.
2. `py IrSimSerial.py COM6`
3. On the ESP32: `set simulate 0`, `reboot`, press `2`.

The server log shows the request and identification arriving at 300 baud, then
the data block at 9600.

### 5. The PCB

Same as 4 but against a real meter, with `RealIrHead` on the PCB's IR pins.
To check the PCB's readings, read the same meter from the laptop with an
optical probe and `py ir_poll_test.py --serial COM6`. That uses the same
handshake as the firmware, so the two should agree.

## Known firmware issues this exposes

Found while building the fake meter. Not fixed here.

- **Only the first read ever works with `SimulatedIrHead`.** It moves to `DONE`
  after the data block and never goes back to `AWAITING_REQUEST`, so every poll
  after the first times out with "meter never responded".
- **Only the first read works on a real UART.** After a read, `RealIrHead` stays
  at the negotiated 9600 baud, but a meter goes back to 300 baud. The next
  `/?!` goes out at the wrong speed. `Iec6205621Reader::handshake()` needs to
  `setBaudRate(300)` before sending the request.
- **The ETX and BCC after the data block are left in the receive buffer.** The
  reader stops at `!\r\n`, so on a real UART the next identification message
  starts with those two bytes and fails the `'/'` check. `TcpBus` hides this
  because it throws away stale bytes before each send. `RealIrHead` doesn't.
- **The BCC is never checked**, so a corrupted reading over real IR would be
  accepted.

With `IrSimTCP.py` (3) all reads work, because of the `TcpBus` behaviour
above. With `IrSimSerial.py` (4) expect the first read to work and later ones
to fail until the first three are fixed. Reproducing that is part of the point.
