# IR node hardware runs, 2026-10-09 to 2026-10-16

Bench runs of the IR node (`Iec6205621Reader`, IEC 62056-21 mode C) on an
ESP32-C3 SuperMini, in three steps: a simulated head through the whole chain
to MQTT, the IrSim fake meter over Wi-Fi through the whole chain, and finally
two boards talking over real infrared light with PiggyMeter heads. There is
no physical electricity meter in any run: the meter is always `IrSim`, either
as a Python script or as `IrSim/board_meter` firmware on a second board.

| | |
|---|---|
| Dates | 2026-10-09 (R1, R2), 2026-10-15 (R3 to R6), 2026-10-16 (R7) |
| Run by | Emmanuel Go, with Claude Code driving the serial sessions |
| Firmware | `end_node` and `lilygo_lora` from the `manny` branch on each day. R7 is `31358cb` |
| End node | ESP32-C3 SuperMini, IR node (boot key `2`). R3 to R7 on the base board with a PiggyMeter head |
| Fake meter | R2: `IrSim/IrSimTCP.py` on a laptop joined to the C3's AP. R3 to R7: Lolin S2 Mini running `IrSim/board_meter` with a second PiggyMeter head (S2 RX 11, TX 12, VCC 3V3) |
| Substation, gateway | R1, R2: two LilyGo LoRa boards (substation and gateway), gateway on a phone hotspot, MQTT to `broker.hivemq.com`, topic `qut_ems_project_888/ems/ZoneA/meters`. R3 to R7: not connected |
| Serial capture | Not saved. Results below are from notes taken during the runs |

## Results

| Run | Checks | Result |
|---|---|---|
| R1 Simulated head, end to end (`simulate 1`) | A canned EM211 session is read on the C3 and the reading reaches MQTT through ESP-NOW, the substation, LoRa and the gateway | Pass |
| R2 IrSim over Wi-Fi, end to end (`simulate 2`) | The reader runs the full IEC 62056-21 handshake against `IrSimTCP.py` over TCP, and the reading, with kWh rising on every poll, reaches MQTT | Pass |
| R3 Polarity over real light | Which way round the PiggyMeter's TX and RX are | Pass. Neither needs inverting (`ir_tx_inv 0`, `ir_rx_inv 0`) |
| R4 300 baud handshake over real light | Request and identification between the two heads | Pass, every time |
| R5 9600 baud data block over real light | The data block after the switch to 9600 | Partial. One full read got through, the rest arrived garbled. Distance and angle seemed to matter |
| R6 Base board IR header | The head on header J3, next to the C3's GPIO 20 and 21 | Fail. J3 is not connected to GPIO 20/21 on this board (see Findings 1) |
| R7 9600 baud after the fix in `31358cb` | Same as R5, with the UART fully restarted on each baud change | Pass. Every poll read over real light |

### R1 and R2 End to end

Three boards on a powered USB hub, one serial monitor each, because every
board asks for its role on each power-up. The C3 was set with `sub_mac` to the
substation's MAC. The gateway joined a 2.4 GHz phone hotspot set with
`set wifi_ssid` and `set wifi_pass`, and the readings were watched on the
laptop with an MQTT client.

For R2 the laptop joined the C3's soft AP and ran `IrSim/IrSimTCP.py`. While it
is on that AP the laptop has no internet, so a second device (or the same
laptop afterwards) is needed to watch MQTT.

### R3 to R6 Real light

The C3 reader faced the S2 Mini fake meter, heads a few cm apart, both UP
arrows up. The S2's serial monitor shows each message it receives and sends,
which is how R3 to R5 were judged. `simulate 0` and `poll_ms 5000` on the C3.

### R7 The 9600 fix

R5's garbling came from the reader, not the light. `RealIrHead` changed baud
with `begin()` on the running UART, which read the 9600 data block garbled
from the second session on. `31358cb` ends the UART first and starts it
again, as `board_meter` already did. After that every poll read over light.

## Not covered

- **Real light, end to end.** R3 to R7 stop at the C3. A reading over real IR
  reaching MQTT has not been run.
- **A real electricity meter.** Every run reads `IrSim`. Section 6 of
  `IrSim/README.md`, the PCB against a real meter, has not been done.
- **The firmware merged on 2026-10-22.** PR #22 (readiness checks, including
  the IR head probe and UART loopback) and PR #23 (delivery ACKs) came after
  these runs. None of the runs above used them.
- **`test_board_ir_head` (HW-04).** The IR head probe tests have not been run
  on a board.

## Findings

1. **Base board header J3 is not connected to GPIO 20/21.** A pin pair scanner
   sketch found no link from J3 pins 7 and 8 to the C3's GPIO 20 and 21,
   likely a bad solder joint. For R3 to R7 the head was wired through header
   J2 to GPIO 1 (RX) and GPIO 0 (TX) instead.

   | | Pins | Where it is set |
   |---|---|---|
   | PCB design, and `main` | RX 20, TX 21 | `IR_RX_PIN`, `IR_TX_PIN` in `lib/config/nvs_config/ir_loader.cpp` |
   | These runs, on the faulty board | RX 1, TX 0, through J2 | The same two constants, changed in a local build only, never committed |
   | `test_board_ir_head` (HW-04) | RX 20, TX 21 | `WIRED` in `test/test_board_ir_head/test_main.cpp` |

   The pins are constants, not NVS keys, so they can't be changed over
   serial. On a board with this fault, `main` as it stands finds no head: since
   PR #22, `init()` then fails and the IR node does not become ready. Until J3
   is fixed, either wire the head straight to the C3's GPIO 20 and 21, or
   change the constants (and `WIRED` for HW-04) in a local build. Don't commit
   1/0. GPIO 1 is also the RS485 node's RX pin since `002f124`.
   *Open:* the EE team needs to check or reflow J3.
2. **9600 baud data block garbled over real light.** *Fixed in `31358cb`*,
   verified in R7.
3. **PiggyMeter polarity.** Neither TX nor RX needs inverting. This differs
   from the EE team's earlier IR probe rig, which needed RX inverted, so the
   firmware sets the two separately (`ir_tx_inv`, `ir_rx_inv`, default 0).
4. **The S2 Mini will not take a normal upload.** `pio run -t upload` stops
   with "Serial data stream stopped" after the bootloader. What works: hold 0
   while plugging it in, then run esptool with `--no-stub` and
   `--after no_reset`, then press RST:

   ```sh
   pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s2 --before default_reset --after no_reset --no-stub write_flash -z <offsets and files>
   ```

   Set `PYTHONUNBUFFERED=1` to see its progress.
5. **The IR LED is invisible.** A phone's selfie camera shows it flashing,
   which is the quickest check that TX is sending.
6. **Opening the C3's COM port resets it** back to the role menu, so the role
   has to be picked again each time a monitor is opened.
7. **Every board asks for its role on every power-up**, with no auto-select,
   so a test needs a serial monitor open on each board.
8. **The gateway does not rejoin Wi-Fi on its own** if the hotspot drops. It
   has to be reset.

## Re-test

To close the gaps above, in this order:

1. **HW-04:** `pio test -e end_node -f test_board_ir_head`, with the PiggyMeter
   head wired to GPIO 20 and 21 and powered (J3 fixed, or jumpered straight to
   the C3's pins).
2. **Real light, end to end:** R7's setup plus R1's substation, gateway and
   MQTT, on the current `main`. Save each board's serial output this time.
3. **A real meter**, when one is available: `IrSim/README.md` section 6.

## Evidence for KZN-TD-01

| DL | What these runs show |
|---|---|
| DL-006 | Active energy import and export read from a test instance over IR (R1, R2, R7) |
| DL-007 | One reading per `poll_ms`, with kWh rising on every poll from `IrSim` (R2) |
| DL-009 | IEC 62056-21 mode C through the C3's UART: 300 baud handshake, baud switch, OBIS parsing, over TCP (R2) and over real light (R4, R7) |
| DL-012, DL-018, DL-019 | IR readings reach MQTT through ESP-NOW, the substation, LoRa and the gateway (R1, R2) |
| DL-024 | Hardware setup, issues and limitations for the IR module (this document) |
