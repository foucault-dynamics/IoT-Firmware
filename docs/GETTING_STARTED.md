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
pio run -e end_node -e lilygo_lora     # proves compilation
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

Common flashing issues:

- The end node waits for the serial monitor before it does anything, so it won't work if no serial monitor is connected.
- If the C3 will not take an upload, hold its BOOT button while plugging it in.
  That forces the bootloader, and the upload then works.
- If no output is shown in the Serial monitor, click the RST button on the board. 

## 3. First boot:

The board prints a menu and waits for one key.

| Image | Key | Role | Log prefix |
|---|---|---|---|
| `end_node` | `1` | RS485 node (Modbus RTU or DLMS/COSEM meter) | `[RS485]` |
| `end_node` | `2` | IR node (optical port, IEC 62056-21) | `[IR]` |
| `end_node` | `3` | CV node (ESP32-CAM reading the display) | `[CV]` |
| `lilygo_lora` | `4` | Substation | `[Substation]` |
| `lilygo_lora` | `5` | Gateway | `[Gateway]`, `[WiFi]`, `[MQTT]` |

The menu comes back on every boot.

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

| Setting | Rule |
|---|---|
| `espnow_chan` | Same on the end node and the substation. Both default to 6, except the CV node which defaults to 1 |
| `lora_*` keys | Same on the substation and the gateway, and `lora_band` legal in your region |
| `lib/interfaces/shared/shared_payload.h` | Every board flashed from the same copy. Change it and reflash everything |

## 5. End to end run, with the Modbus simulator

This takes one reading from a simulated Modbus meter all the way to an MQTT
broker on the internet, and then shows you how to watch it arrive. You need one
ESP32-C3, two LilyGo boards, a laptop, and a 2.4 GHz Wi-Fi network with
internet access for the gateway. OPTIONAL: RS485 converter and SP3485 transceiver.

For the optional serial run, wire the SP3485 to the C3 like this:

| SP3485 pin | ESP32-C3 pin |
|---|---|
| GND | GND |
| VCC | 3V3 or 5V |
| RTS (DE/RE) | GPIO 10 |
| TXD (RO) | GPIO 1 (RX) |
| RXD (DI) | GPIO 9 (TX) |

```text
ModbusSimTCP.py ──Wi-Fi──► RS485 node ──ESP-NOW──► substation ──LoRa──► gateway ──Wi-Fi──► broker ──► mosquitto_sub
 (laptop)                  (C3)                    (LilyGo)             (LilyGo)          (internet)
 
 OPTIONAL:
 
 ModbusSimSerial.py ──RS485──► RS485 node ──ESP-NOW──► substation ──LoRa──► gateway ──Wi-Fi──► broker ──► mosquitto_sub
	 (laptop)                  (C3)                    (LilyGo)             (LilyGo)          (internet)
```

We are going to start from the top but there is no strict order of setup, you can setup each node independently and if instructions are followed, the system should work.

### 5.1 Gateway: connect it to the internet (LilyGo board) {#gs_gateway}

Compile and flash the `lilygo_lora` environment to a Lilygo board, open the monitor and press `5`. Then point it at your
Wi-Fi network and reboot:

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
python3 -m pip install pyserial
```

Flash `end_node` to the C3, open its monitor and press `1`. It boots in its
default real meter mode and quietly fails to read, which is fine for now.
Configure it for the simulator and reboot:

```text
set reader 3
reboot

OPTIONAL:
set reader 0
reboot
```

#### MODBUS OVER WIFI:
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

#### MODBUS OVER SERIAL
Press `1` again. `reader 0` is Modbus RTU over a real RS485 line: the node
talks to the SP3485 on `Serial1`, and the laptop plays the meter through a
USB to RS485 converter. It is also the default, so a fresh board is already in
this mode and `set reader 0` is only needed to come back from `reader 3`.

Wire the SP3485 before that reboot, as in the table at the start of this
section. The pins are fixed in `rs485_loader.cpp`, not NVS keys. At boot the
node checks the transceiver before anything else, and unlike the TCP mode a
failure here is fatal: it prints `[RS485] Bus init failed, idling` after one
of the `[SP3485]` lines in [Troubleshooting](#gs_trouble), and does nothing
more until you fix the wiring and reboot.

Then, on the laptop:

1. Plug in the USB to RS485 converter and wire its A to the SP3485's A, its B
   to B, and its GND to the C3's GND, so both ends share a ground.
2. Find the converter's port with `pio device list`, or `ls /dev/cu.*` on
   macOS. A CH340 based converter shows up as something like
   `/dev/cu.wchusbserial10`.
3. Start the simulator on that port, with the venv active:

   ```sh
   python3 ModbusSimSerial.py /dev/cu.wchusbserial10
   ```

   Leave the port off and it defaults to `/dev/cu.wchusbserial10`.

The node's other settings already match the simulator: 9600 baud `8N1`, slave
address 1 and the same register map, so nothing else needs setting. Until the
simulator answers, the node prints `[RS485] Meter check failed, retrying in
5 s`. Once it does, the node prints `[RS485] Ready`, reads straight away and
then every 60 s, the same as the TCP run. If the check never passes with the
simulator running, swap A and B, since converters do not agree on which is
which.

The laptop stays on its normal network in this mode, so you can watch MQTT
from the same laptop.

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
