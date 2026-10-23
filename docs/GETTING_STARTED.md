# Getting started

This page gives a rundown of how to start running the project on given hardware for on-boarding developers or any relevant non-experienced party. It covers installing the tools, building and flashing each board,
getting one reading all the way from a meter node to MQTT, running the tests,
and how the code in `src/` is put together.

## Platform IO image for each node

```text
  meter ──► end node ──ESP-NOW──► substation ──LoRa──► gateway ──MQTT──► broker
            ESP32-C3              LilyGo                LilyGo
            end_node image        lilygo_lora image     lilygo_lora image
```

There are two firmware images. `end_node` runs on the ESP32-C3 and contains the
three meter nodes (RS485, IR and CV). `lilygo_lora` runs on the LilyGo LoRa
board and contains the substation and the gateway. Every board of one kind gets
the same image, and picks its role at boot from a serial menu.

## 1. Install the tools

| Tool | Needed for | Install |
|---|---|---|
| PlatformIO Core | Building, flashing, serial monitor, tests | `brew install platformio`, or `pipx install platformio` |
| Python 3 | ModbusSim, IrSim and DlmsSim | Usually already installed. Each simulator uses its own venv |
| Espressif QEMU | The unit tests | `tools/setup_qemu.sh`, see [Running the tests](#gs_tests) |
| Doxygen 1.18.0 and Graphviz | Building this site locally | `brew install doxygen graphviz` |
| A USB-C data cable | Flashing | For flashing the ESP32-C3 supermini or LilyGo board (subject to hardware)|
| A Micro-USB data cable| Flashing| Some LilyGo boards only have a Micro USB port (subject to hardware)|

PlatformIO downloads the ESP32 toolchain, the Arduino core and every library in
`lib_deps` (ArduinoJson, LoRa, PubSubClient) the first time you build, so the
first build takes a few minutes.

```sh
git clone git@github.com:foucault-dynamics/IoT-Firmware.git
cd IoT-Firmware
pio run -e end_node -e lilygo_lora     # proves the toolchain works
```

## 2. Build, flash and open the serial monitor

| Command | Does |
|---|---|
| `pio run -e end_node` | Builds the end node image |
| `pio run -e end_node -t upload` | Builds and flashes the connected ESP32-C3 |
| `pio run -e lilygo_lora -t upload` | Builds and flashes the connected LilyGo |
| `pio device list` | Lists serial ports, to tell boards apart |
| `pio run -e end_node -t upload --upload-port /dev/cu.usbmodem1101` | Flashes one board when several are plugged in |
| `pio device monitor --echo` | Opens serial at 115200 baud, showing what you type |

Two things catch everyone the first time:

- The end node waits for the serial monitor before it does anything
  (`while(!Serial)` in `src/end_node_main.cpp`), because the C3 talks over its
  own USB port and anything printed before a monitor is attached is lost. Open
  the monitor and the boot menu appears about two seconds later.
- If the C3 will not take an upload, hold its BOOT button while plugging it in.
  That forces the bootloader, and the upload then works.
- If no output is shown in the Serial monitor, click the RST button on the board. 

## 3. First boot: choose a role

The board prints a menu and waits for one key.

| Image | Key | Role | Log prefix |
|---|---|---|---|
| `end_node` | `1` | RS485 node (Modbus RTU or DLMS/COSEM meter) | `[RS485]` |
| `end_node` | `2` | IR node (optical port, IEC 62056-21) | `[IR]` |
| `end_node` | `3` | CV node (ESP32-CAM reading the display) | `[CV]` |
| `lilygo_lora` | `4` | Substation | `[Substation]` |
| `lilygo_lora` | `5` | Gateway | `[Gateway]`, `[WiFi]`, `[MQTT]` |

The menu comes back on every boot. That is temporary: it stands in for the
module ID pin on the final end node PCB and whatever ID mechanism the LoRa PCB
gets, (FUTURE DEVELOPMENT).

## 4. Configure a board over serial

Settings live in the board's NVS (Non-Volatile Storage) (its small key value flash store), and any key
not set already falls back to the firmware default in
`lib/config/nvs_config/*_loader.cpp` or `lib/config/secrets/secrets.h`. Type
these into the monitor:

| Command | Does |
|---|---|
| `set poll_ms 5000` | Stores a number |
| `set wifi_ssid "MyNetwork"` | Stores a string, quotes required |
| `clear` | Wipes every setting back to the defaults |
| `reboot` | Restarts the board |

Nothing takes effect until `reboot`, because each node only loads its config
once in setup. A number written to a key that is read as a string (or the other
way round) is saved but never used, so check the type in
[NVS config keys](../lib/config/nvs_config/NVS_KEYS.md), which lists every key and more information on NVS.

Three settings have to agree between boards, or readings silently go nowhere.
End nodes need no substation address: they hear its beacon and join it at boot.

| Setting | Rule |
|---|---|
| `espnow_chan` | Same on the end node and the substation. Both default to 6, except the CV node which defaults to 1 |
| `lora_*` keys | Same on the substation and the gateway, and `lora_band` legal in your region |
| `lib/interfaces/shared/shared_payload.h` | Every board flashed from the same copy. Change it and reflash everything |

## 5. End to end run, with the Modbus simulator

This takes one reading from a simulated Modbus meter all the way to an MQTT
broker on the internet, and then shows you how to watch it arrive. You need one
ESP32-C3, two LilyGo boards, a laptop, and a 2.4 GHz Wi-Fi network with
internet access for the gateway.

```text
ModbusSimTCP.py ──Wi-Fi──► RS485 node ──ESP-NOW──► substation ──LoRa──► gateway ──Wi-Fi──► broker ──► mosquitto_sub
 (laptop)                  (C3)                    (LilyGo)             (LilyGo)          (internet)
```

Bring the boards up in this order, from the broker end back towards the meter,
so each board has somewhere to send to as soon as it starts.

### 5.1 Gateway: connect it to the internet (LilyGo board) {#gs_gateway}

Flash `lilygo_lora`, open the monitor and press `5`. Then point it at your
Wi-Fi and reboot:

```text
set wifi_ssid "MyNetwork"
set wifi_pass "my wifi password"
reboot
```

Press `5` again. A working gateway prints:

```text
[WiFi] Connecting to MyNetwork.....
[WiFi] Connected!
[WiFi] IP: 192.168.1.23
[System] Gateway Ready. Mode: LoRa task + MQTT loop.
[MQTT] Connected to Broker
GATEWAY MQTT CONNECTED Waiting for LoRa...
```

To change the Wi-Fi network or password later, run the same two `set`
commands with the new values and `reboot`. They are stored on that board only,
so every gateway needs them set once.

Things the Wi-Fi has to be:

- **2.4 GHz.** The ESP32 has no 5 GHz radio. On an iPhone hotspot, turn on
  Maximise Compatibility to force 2.4 GHz.
- **WPA2 Personal, a plain password.** Networks where you log in with a
  username, such as eduroam or the QUT network, and networks with a sign in web
  page, will not work. A phone hotspot is the easiest option on campus.
- **Short enough to type in.** The serial command line is 63 characters, so
  `set wifi_pass "..."` fits a password of up to 47 characters. Longer lines
  lose their closing quote and are rejected with `[CFG] malformed quoted value`.

If you see `[WiFi] Connect Timeout!`, the SSID or password is wrong or the
network is 5 GHz only. The gateway gives up on Wi-Fi after about 10 s and then
keeps retrying the broker every 5 s.

**Which broker it publishes to.** Out of the box the gateway publishes to the
public HiveMQ broker, which needs no account:

| Key | Default | Meaning |
|---|---|---|
| `mqtt_host` | `broker.hivemq.com` | Broker hostname |
| `mqtt_port` | `8883` | Broker port. The gateway always uses TLS, so this must be a TLS port |
| `mqtt_topic` | `qut_ems_project_888/ems/ZoneA/meters` | Topic every reading is published on |
| `mqtt_user` | empty | Username. Empty connects without logging in |
| `mqtt_pass` | empty | Password |

A public broker means anyone who knows the topic can read the readings, and
publish to it too, which is fine for testing and not for real meter data. To
use your own broker, for example a free HiveMQ Cloud cluster, set its details
the same way:

```text
set mqtt_host "abc123.s1.eu.hivemq.cloud"
set mqtt_user "gateway"
set mqtt_pass "broker password"
reboot
```

The gateway always connects over TLS (`WiFiClientSecure`), so a broker that
only listens on plain port 1883 will not work. It also skips checking the
broker's certificate for now, see the `TODO` in `gatewaySetup()`.

Keep real passwords out of `lib/config/secrets/secrets.h`. That file sets the
defaults for every board, but it is tracked in git, so set credentials over
serial instead and they stay in the board's NVS.

**No internet on your desk?** `set mqtt_on 0` and `reboot`. The gateway then
skips Wi-Fi and MQTT and just prints each reading it receives, which is enough
to check the radio side of the network.

### 5.2 Substation

Flash the second LilyGo, open its monitor and press `4`. It starts
beaconing so end nodes can find it:

```text
>>> MAC Address: F0:24:F9:92:FB:C0 <<<
SUBSTATION: Beaconing on channel 6, epoch 3A91C07E
```

Each end node that joins shows up as a `[Substation] Joined UID: ...` line.

### 5.3 RS485 node and the Modbus simulator

On the laptop, set up the simulator once:

```sh
cd ModbusSim
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install pymodbus
```

Flash `end_node` to the C3, open its monitor and press `1`. It boots in its
default real meter mode and quietly fails to read, which is fine for now.
Configure it for the simulator and reboot:

```text
set reader 3
reboot
```

Press `1` again. `reader 3` is Modbus TCP: the node starts its own Wi-Fi
network, `kaizen-rs485` with password `kaizen123`, and waits for the simulator
to appear on it.

Set everything before that reboot. While the node waits for the simulator it
is still inside setup, so it does not read serial commands, and it prints
`[RS485] TcpBus init failed.` over and over until the simulator answers.

Then, on the laptop:

1. Join the `kaizen-rs485` Wi-Fi network. The first device to join gets
   `192.168.4.2`, which is the node's default `tcp_host`. Check yours with
   `ipconfig getifaddr en0` on macOS. If it is different, start the simulator
   anyway so setup finishes, then `set tcp_host "192.168.4.3"` and `reboot`.
2. Start the simulator, with the venv active:

   ```sh
   python3 ModbusSimTCP.py
   ```

The node connects, reads straight away, and then reads every 60 s
(`POLL_INTERVAL_MS` in `rs485_node.cpp`, the `poll_ms` key does not change
it). The simulator changes its values on every poll, so each reading differs
from the last.

While the laptop is on `kaizen-rs485` it has no internet, so watch MQTT from a
second device, such as another laptop or a phone, or give the laptop a wired
or USB tethered connection as well.

### 5.4 What you should see

```text
[RS485] Ready                                            end node
import: ...                                              end node
export: ...                                              end node
voltage: ...                                             end node
[Substation] Buffered UID: ... | SEQ: 1 | Total: 1       substation
[Substation] Relay SUCCESS | UID: ... | SEQ: 1 | ...     substation
=> Parsed Data -> UID: ... | SEQ: 1 | Volt: ...          gateway
=> JSON: {"ts":"...","uid":"...","seq":1,...}            gateway
DATA FWD, UID: ..., PUB: SUCCESS                         gateway
```

`[RS485] Ready` prints once the meter answers its probe, and only then does the
node start reading. The IR and CV nodes print `[IR] Ready` and `[CV] Ready` the
same way. If one board's line is missing, the problem is on the hop into that
board, see [Troubleshooting](#gs_trouble).

### 5.5 Listen to MQTT {#gs_mqtt}

Install the Mosquitto command line clients:

```sh
brew install mosquitto                  # macOS
sudo apt install mosquitto-clients      # Debian or Ubuntu
```

Subscribe to the gateway's topic on the default broker:

```sh
mosquitto_sub -h broker.hivemq.com -p 8883 --tls-use-os-certs \
  -t 'qut_ems_project_888/ems/ZoneA/meters' -v
```

| Flag | Why |
|---|---|
| `-h`, `-p` | The broker, the same as the gateway's `mqtt_host` and `mqtt_port` |
| `--tls-use-os-certs` | Turns on TLS and trusts the certificates your OS already has. Without a TLS flag `mosquitto_sub` talks plain MQTT, which port 8883 refuses |
| `-t` | The topic, the same as the gateway's `mqtt_topic`. Keep the single quotes so the shell leaves it alone |
| `-v` | Prints the topic in front of each message |

It prints nothing until a reading is published, then one line per reading:

```text
qut_ems_project_888/ems/ZoneA/meters {"ts":"2026-10-10T14:03:00+10:00","uid":"...","seq":1,"kwh_import":...,...}
```

For your own broker, add `-u` and `-P` with its username and password. Add
`-d` to see the connect and subscribe steps when nothing arrives, and `-C 1`
to exit after the first message. Topics are case sensitive, and
`-t 'qut_ems_project_888/#'` subscribes to everything under the project
prefix, which helps when you are not sure of the exact topic.

If you prefer a GUI, [MQTT Explorer](https://mqtt-explorer.com) shows the same
thing: connect to `broker.hivemq.com` on port 8883 with encryption (TLS) on, and
the topic tree fills in as readings arrive.

### 5.6 Other meters

Once the simulator run works, swap the meter end for something closer to the
real thing. The substation and gateway stay as they are.

| To test | Settings on the end node | Then |
|---|---|---|
| A real Modbus meter | `set reader 0`, plus `meter_model`, `baud`, `slave_addr` | SP3485 on GPIO 1 RX, 9 TX, 10 DE/RE |
| A real DLMS/COSEM meter | `set reader 5`, plus the `dlms_*` keys | Same SP3485 wiring |
| DLMS/COSEM against DlmsSim | `set reader 6`, `tcp_port 5022`, `reboot` | Same softAP idea as the Modbus run, run `DlmsSim/DlmsSimTCP.py`. See `DlmsSim/README.md` |
| IR node, built in simulated meter | Press `2` | Defaults to `simulate 1`, no wiring and no laptop |
| IR node against IrSim | Press `2`, `set simulate 2`, `reboot` | Same softAP idea, run `IrSim/IrSimTCP.py`. See `IrSim/README.md` |
| The CV node | Press `3`, and `set espnow_chan 6` | Needs an ESP32-CAM running AI-on-the-edge-device. See `lib/config/secrets/README.md` |

### 5.7 IR node {#gs_ir}

The IR node reads a meter's optical port with IEC 62056-21 mode C: a request
at 300 baud 7E1, the meter's identification, an ACK that switches both sides
to the speed the meter offers (9600 for IrSim), then a data block with the
import (`1-0:1.8.0`) and export (`1-0:2.8.0`) kWh. The `simulate` key picks
what is at the other end. The substation and gateway are set up as in 5.1 and
5.2.

| `simulate` | Meter | You need |
|---|---|---|
| `1` (default) | `SimulatedIrHead`, a canned session inside the firmware | The C3 only |
| `2` | `IrSim/IrSimTCP.py` on a laptop, over Wi-Fi | The C3 and a laptop |
| `0` | A real head on the UART, `RealIrHead` | The C3, a PiggyMeter head, and a meter or `IrSim/board_meter` on a second board |

Start with `1`, then `2`, then `0`, so each step only adds one new thing that
can go wrong. Every mode reads every `poll_ms`, 60 s by default. For a demo,
`set poll_ms 5000` and `reboot`, and set it back to `60000` after.

**Simulated meter (`simulate 1`).** Flash `end_node`, open the monitor and
press `2`. Nothing else: readings start straight away. The values are fixed,
so every reading is the same.

**IrSim over Wi-Fi (`simulate 2`).** The same idea as the Modbus run in 5.3.

1. In the C3's monitor, `set simulate 2`, `reboot`, and press `2`. The node
   starts its own network, `kaizen-rs485` (`ap_ssid`), and waits for the fake
   meter. Set everything before this reboot: while it waits it does not read
   serial commands.
2. Join `kaizen-rs485` from the laptop. The first device gets `192.168.4.2`,
   the default `tcp_host`. If yours differs, `set tcp_host "192.168.4.3"` and
   `reboot`.
3. Run the fake meter. It needs no packages:

   ```sh
   cd IrSim
   python3 IrSimTCP.py         # Windows: py IrSimTCP.py
   ```

   Windows Firewall may ask about Python the first time. Allow it on private
   networks, or the C3 can't connect.

Import and export go up on every read, so two readings are never the same and
a stuck reading is obvious. As in 5.3, the laptop has no internet while it is
on `kaizen-rs485`, so watch MQTT from a second device.

**Real IR head (`simulate 0`).** Wire a PiggyMeter head to the C3:

| PiggyMeter | ESP32-C3 |
|---|---|
| VCC | 3V3 (not 5V) |
| GND | GND |
| TX | GPIO 20 (`IR_RX_PIN`) |
| RX | GPIO 21 (`IR_TX_PIN`) |

The pins are constants in `lib/config/nvs_config/ir_loader.cpp`, not NVS keys.
On the one base board tested so far, header J3 does not reach GPIO 20/21, so
wire to the C3's pins directly, or see the J3 note in
[the IR test report](hw-tests/2026-10-09-ir-node/README.md). Then
`set simulate 0`, `reboot` and press `2`. Neither `ir_tx_inv` nor `ir_rx_inv`
is needed for the PiggyMeter, both stay `0`.

With no real meter, flash `IrSim/board_meter` to a second board with its own
PiggyMeter head and face the two heads at each other, a few cm apart, both UP
arrows up. See section 5 of `IrSim/README.md` for its wiring and flashing.
Keep bright light off the head while the node starts: its start-up check
reads light on the sensor as a fault.

**What you should see** on the C3, then the same substation and gateway lines
as in 5.4:

```text
[IR] Ready
[IR] import=1234.567 export=45.123 seq=1
```

If a read fails, the line before `[IR] Meter read failed.` says which step
broke:

| Line | Usually |
|---|---|
| `[IEC62056-21] meter never responded to request message` | No meter, heads not facing each other, IrSim not running, or the wrong pins |
| `[IEC62056-21] identification message malformed` | Noise or partial light. Move the heads closer and line them up |
| `[IEC62056-21] no data block received after ACK` | The baud switch failed, or the light dropped out after the handshake |
| `[IEC62056-21] BCC mismatch` | Bits corrupted on the way, usually light or distance |
| `[IEC62056-21] data block missing required OBIS code` | The meter answered but has no `1-0:1.8.0`, for example a meter that uses short labels like `1.8.0` |

`IrSim/README.md` has more ways to test, from laptop only up to a USB to serial
adapter in place of the light.

## 6. How the code in src/ fits together {#gs_flow}

### Two entry files, one pattern

`platformio.ini` decides which files in `src/` go into each image, through
`build_src_filter`:

| Image | Entry file | Node files built in |
|---|---|---|
| `end_node` | `src/end_node_main.cpp` | `rs485_node.cpp`, `ir_node.cpp`, `cv_node.cpp` |
| `lilygo_lora` | `src/lora_main.cpp` | `substation.cpp`, `gateway.cpp` |

Both entry files are the same shape. They hold Arduino's `setup()` and
`loop()` and nothing else, and all they do is pick a role and hand over to it:

```text
setup()                                  loop(), forever
  Serial.begin(115200)                     nvsConfigPollSerial()   serial set/clear/reboot
  wait for the monitor                     switch (moduleType)
  moduleType = readModuleType()              case Rs485Node: rs485NodeLoop()
  switch (moduleType)                        case IrNode:    irNodeLoop()
    case Rs485Node: rs485NodeSetup()         ...
    case IrNode:    irNodeSetup()
    ...
```

`src/nodes/nodes.h` declares one `xxxSetup()` and `xxxLoop()` pair per role,
and each pair lives in its own file in `src/nodes/`.

### The three meter nodes

All three start the same way. Setup loads the config, builds the node's bus and
reader, and hands them to `endNodeBringUp()` (`lib/bringup/end_node_bringup`),
which checks the board's own peripherals in one fixed order:

```text
seqCounterBegin()          NVS for the sequence counter
wifiRadioStartAp()         only CV, and the TCP simulator modes
bus->init()                fatal, except a TCP simulator bus whose host may not be up yet
reader->init(*bus)
SubstationLink init()      ESP-NOW
SubstationLink join()      not fatal, send() joins later
```

Any fatal step logs `[TAG] ..., idling` and the loop then does nothing. Setup
then creates a `MeterGate`, and every loop starts with `gate->ready()`:

```text
not ready ──probe passes──► [TAG] Ready ──► read every cycle
    ▲  └─ probe fails: retry in 5 s              │
    └──────────── 3 failed reads in a row ◄──────┘
```

`ready()` probes the meter with the reader's `probe()`, which proves a meter
answers without reading a value, and does no I/O once ready. Every read is
reported back with `gate->reportRead()`.

All three end with the same step: fill the `Payload`, take the next sequence
number with `seqNext()`, and hand it to `SubstationLink::send()`, which joins
the substation first if needed.

**`rs485_node.cpp`** picks its bus and reader from the `reader` key:

| `reader` | Module | Reader |
|---|---|---|
| `0` Modbus RTU | `Sp3485` on `Serial1` | `ModbusRtuReader` |
| `3` Modbus TCP | softAP, then `TcpBus` | `ModbusRtuReader` |
| `5` DLMS/COSEM | `Sp3485` on `Serial1` | `DlmsCosemReader` |
| `6` DLMS/COSEM over TCP | softAP, then `TcpBus` | `DlmsCosemReader` |

Its loop is a small state machine, so each call does one step and returns:

```text
READ ──all three getters ok──► SEND ──► SLEEP ──60 s passed──► READ
 └── any getter fails: report it to the gate, stay in READ, try again next loop
```

**`ir_node.cpp`** picks an `IrHead` from the `simulate` key (`0`
`RealIrHead`, `1` `SimulatedIrHead`, `2` `TcpIrHead`) and puts an
`Iec6205621Reader` on it. One optical session gives import and export together,
so instead of a state machine it does one read and send per `poll_ms`, the first
straight after `[IR] Ready`.

**`cv_node.cpp`** starts a softAP for the camera to join and puts an `HttpBus`
and a `CamHttpReader` on it. Its gate probes the camera every 5 s until it
answers, then it reads once per `poll_ms`, the first straight after
`[CV] Ready`. It sends import, plus export when `cam_flow_exp` names a second
number on the camera, otherwise export is 0.

### The substation

```text
substationLoop()
  beaconIfDue()               broadcast a beacon once a second
  handleIncoming()            drain ESP-NOW
    Join       ──► add or refresh the node, send a JoinAck
    reading    ──► readingBufferPush(), then an AckPayload back to the node
    same seq   ──► ACK again, not buffered twice
  relayStep()                 one step of the LoRa relay, never waits on it
    relay in flight: LoRaLink::pollSend()
      Acked   ──► readingBufferPop()
      Failed  ──► keep it, back off 30 s (RETRY_BACKOFF_MS)
      Pending ──► return
    idle, not backing off: readingBufferPeek(), LoRaLink::beginSend()
```

The ESP-NOW ACK only goes out once the reading is in the buffer, so the end
node knows the substation kept it rather than only that its radio heard it. A
reading from a node that has not joined, or one the buffer refuses, gets no
ACK, and the node rejoins and resends it. Because the relay runs a step per
pass instead of blocking for its retries, the substation keeps beaconing and
ACKing while it waits on the gateway, and each ACK comes back within one pass.

A reading only leaves the buffer once the gateway has acknowledged it, and new
readings keep being buffered during the back off, so a gateway outage costs
latency rather than data until the buffer fills.

### The gateway

The gateway runs two things at once:

```text
loraRxTask (FreeRTOS task)               gatewayLoop() (Arduino loop)
  every 5 ms:                              reconnect MQTT if needed (blocking)
    LoRaLink::receivePacket()              xQueuePeek(readings)
      checks the size, sends the ACK       print it
    xQueueSend(readings, reading + RSSI)   publish JSON to mqtt_topic
                                           on success, xQueueReceive (drop it)
```

They are split because reconnecting to MQTT blocks for seconds at a time. With
LoRa in its own task, the radio keeps receiving and ACKing during a broker
outage, and readings wait in the 32 slot queue instead of the substation
timing out.

### Where to make common changes

| Change | Files |
|---|---|
| A default value | That node's `lib/config/nvs_config/*_loader.cpp`, or `secrets.h` for credentials and addresses |
| A new meter protocol | A `Reader` in `lib/protocols/<name>/` with a `probe()`, its config struct in `lib/config/node_config/`, its keys in the loader, a `ReaderType` value, and a `case` in `rs485NodeSetup()` |
| A new bus | A `Module` in `lib/buses/<name>/`, created in the node's setup. A UART bus checks its pins and UART with `lib/buses/bus_checks` |
| A new role | A setup and loop pair in `nodes.h`, a file in `src/nodes/`, a `ModuleType` value and menu line and two `case`s in the entry file, and the file added to that image's `build_src_filter` |
| The packet format | `shared_payload.h`, then reflash every board |

## 7. Running the tests {#gs_tests}

The unit tests run the real firmware code inside an emulated ESP32-C3, so they
need no board:

```sh
tools/setup_qemu.sh                          # once
export ESP_QEMU=~/.kaizen/qemu/esp-develop-9.2.2-20260417/qemu/bin/qemu-system-riscv32
pio test -e test_c3 --without-uploading
```

`ESP_QEMU` has to be exported in the shell you run `pio test` from, otherwise
every suite fails before it starts. Put the export in your shell profile.

Other checks worth running before a pull request. CI builds both images and
runs the whole QEMU suite, the rest are local only:

| Command | Checks |
|---|---|
| `pio run -e end_node -e lilygo_lora` | Both images still build (run by CI) |
| `pio test -e test_c3 --without-uploading -f test_qemu_nvs` | One suite, faster while iterating |
| `pio test -e end_node -f test_board_radio` | The radio tests, on a connected C3 |
| `sh IrSim/native/run.sh` | The IEC 62056-21 reader on the laptop, needs `g++` |
| `doxygen` | Every doc comment is present and well formed, prints nothing when clean |

[Testing](TESTING.md) has the suite list, how a QEMU run works and how to write
a new test. [Commenting convention](COMMENTING.md) has the doc comment rules
`doxygen` enforces.

## 8. Troubleshooting {#gs_trouble}

| Symptom | Likely cause |
|---|---|
| Nothing on serial after flashing the C3 | The board is waiting for a monitor. Open one, or check `pio device list` for the right port |
| `[BOOT] '1' is not 4 or 5` | The board has the other image. Flash `end_node` to a C3 and `lilygo_lora` to a LilyGo |
| `set` seems to do nothing | You have not rebooted, or the value is the wrong type (strings need quotes) |
| `[TAG] NVS unavailable for the sequence counter, idling` | The NVS partition cannot be opened. Reflash with `pio run -t erase` first |
| `[TAG] SoftAP bring-up failed, idling` | `ap_ssid` or `ap_pass` is invalid (the password needs 8 to 64 chars), or `espnow_chan` clashes with a channel already in use |
| `[TAG] Bus init failed, idling` | The bus's own line just before it says why, one of the `[SP3485]`, `[RealIrHead]` or `[HttpBus]` lines below |
| `[TAG] Bus not reachable yet, the meter check will retry` | TCP simulator mode, and the laptop has not joined the softAP or the simulator is not running. Not fatal |
| `[TAG] Reader init failed, idling` | The reader's config is invalid, for example an empty `cam_host` or `cam_flow`, or a bad DLMS address size |
| `[TAG] ESP-NOW init failed, idling` | The radio did not start ESP-NOW, usually a channel the AP already holds differently. Reboot, then check `espnow_chan` |
| `[TAG] Meter check failed, retrying in 5 s` | The meter did not answer its probe. The reader's line just before says how (silence, bad CRC, refused association, no identification, no cam). Check wiring, the meter's address and baud rate, or that the cam has joined |
| `[TAG] Meter lost after 3 failed reads, re-checking` | The meter stopped answering after being ready. The node probes it every 5 s and prints `[TAG] Ready` again when it is back |
| `[SP3485] RX not driven in receive, transceiver missing or miswired` | The SP3485 is missing, unpowered, or its RO is not on the RX pin |
| `[SP3485] RX still driven with DE/RE high, DE/RE not wired` | The DE/RE pin does not reach the SP3485 |
| `[TAG] UART pins invalid (RX n, TX n)` | An RX or TX pin in NVS is not a GPIO on the C3 |
| `[TAG] UART failed to start` | The UART driver did not install. Reboot, and check nothing else holds the UART |
| `[TAG] UART loopback: no echo` or `got 0xNN` | The UART does not echo its own byte internally, so its baud rate or frame format is broken. Reboot, and report it if it persists |
| `[RealIrHead] RX floats, head missing or unpowered` | The IR head is not plugged in, unpowered, or its RX is on another pin. The firmware expects GPIO 20 (RX) and 21 (TX). On the base board tested so far, header J3 does not reach those pins, so wire the head to the C3's pins directly, or through J2 on GPIO 1 and 0 with `IR_RX_PIN` and `IR_TX_PIN` in `ir_loader.cpp` changed in a local build |
| `[RealIrHead] RX held active: light on sensor or wrong ir_rx_inv` | Light is falling on the head's sensor, or the head idles the other way round, so flip `ir_rx_inv` |
| `[RealIrHead] RX unstable` | RX is toggling while idle, usually flickering light or a loose wire |
| `[HttpBus] AP is not up` | The AP did not come up, see the SoftAP line above |
| `[ModbusRTU Reader] Meter present, no diagnostics (exception N)` | Not an error. The meter does not support FC08, so it refused the probe, which still proves it is there |
| End node logs `[Link] No substation beacon heard` | `espnow_chan` differs, or the substation is off or out of range |
| End node logs `[Link] Send failed, rejoining` | The substation went off, out of range or busy. The node rejoins and resends at once, and keeps trying discovery on every reading until one answers |
| End node logs `[Link] No delivery ACK, rejoining` | The substation's radio took the reading but the substation did not buffer it, usually because it rebooted or forgot the join. The node rejoins and resends at once. Persistent means the substation runs an older image without delivery ACKs |
| `[Substation] Dropped reading from unjoined MAC` | The node missed the join. It gets no delivery ACK, so it rejoins and resends straight away. Persistent means the node runs an older image |
| `[Substation] Duplicate UID: ... re-acknowledged` | Not an error. The node resent a reading whose ACK was lost, so it is ACKed again but buffered once |
| `[Substation] Buffer refused UID: ... not acknowledged` | Every node slot in the reading buffer is taken by other UIDs. The node gets no ACK and its send fails |
| `[Substation] Relay FAILED` | Gateway off, `lora_*` settings differ between the two boards, or out of range. The reading is kept and retried after 30 s |
| `[Error] Payload size mismatch!` | The boards were built from different versions of `shared_payload.h` |
| `[WiFi] Connect Timeout!` on the gateway | Wrong `wifi_ssid` or `wifi_pass`, a 5 GHz only network, or a network that needs a username or sign in page. See [Gateway](#gs_gateway) |
| Gateway repeats `[MQTT] Connection Failed` | No Wi-Fi, wrong `mqtt_host` or `mqtt_port`, a broker without TLS, or wrong `mqtt_user` and `mqtt_pass`. `set mqtt_on 0` to test without one |
| `PUB: SUCCESS` on the gateway but `mosquitto_sub` shows nothing | Different broker or topic on the two sides, or `mosquitto_sub` is missing `--tls-use-os-certs`. Rerun it with `-d` |
| `ESP_QEMU is not set` | Run `tools/setup_qemu.sh` and export the line it prints |
