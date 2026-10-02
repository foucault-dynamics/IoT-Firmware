# ModbusSim

Python simulators that stand in for a Modbus meter, so the meter-reading
firmware in `lib/modbus_rtu/` can be developed and debugged with no transceiver
and no meter on the desk.

Both servers speak **RTU framing**, which is what the firmware speaks. The TCP
one carries those RTU bytes over a socket (RTU-over-TCP, not Modbus TCP, so
there is no MBAP header), the serial one puts them on a real serial port.

| File | What it is |
|---|---|
| `ModbusSimTCP.py` | RTU-over-TCP server on `0.0.0.0:5020`. Talk to it from a laptop, or from firmware via `lib/tcp_bus/`. |
| `ModbusSimSerial.py` | RTU server on a serial port, `PORT` hardcoded to `/dev/ttyUSB0`. Closest to the real RS485 wiring. |
| `poll_test.py` | Minimal RTU-over-TCP client. Reads all three registers and prints them. Use it to confirm the server before pointing firmware at it. |

## Register map

Device id (slave address) `1`. Every value is a 32-bit quantity spread over two
consecutive big-endian 16-bit holding registers, read with function code `0x03`
and `count=2`.

| Value | Address | Behaviour on each poll |
|---|---|---|
| Voltage | 0 | Sine wave around 230.0 V, `230 + 5*sin(t/10)`, encoded as an IEEE 754 float |
| kWh export | 2 | Counter starting near 1000, increments by 1 |
| kWh import | 4 | Counter starting near 100, increments by 1 |

The values are rewritten by `meter_action()` on every read, so two polls never
return the same thing. That is deliberate: it makes a stale or cached read
obvious.

These addresses match the `MeterModel::Simulated_Serial` and
`MeterModel::Simulated_Tcp` rows of the `METER_MODELS` table in
`lib/node_config/node_config.cpp`, so firmware pointed at either simulator needs
no address changes.

### Known quirk: the kWh encoding changes after the first poll

`define_device()` seeds all three registers with `DataType.FLOAT32`, but
`meter_action()` re-encodes the two kWh counters with `_encode_uint32()`. So the
kWh registers hold a float only until the first read, and a raw integer
afterwards. Voltage is a float throughout.

Consequences, worth knowing before you chase a firmware bug that is not there:

- `poll_test.py` decodes all three as float32, so its `kwh_export` and
  `kwh_import` lines print nonsense (a denormal near zero) rather than ~1000 and
  ~100. Voltage prints correctly.
- The firmware's `RegisterFormat::ScaledInt`, which is what
  `loadModbusRtuConfig()` currently sets, matches the post-poll integer encoding,
  divided by 1000. `RegisterFormat::IEEE_754Float` matches only the seeded value.

Fixing this means picking one encoding and using it in both `define_device()` and
`meter_action()`.

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install pymodbus pyserial
```

`pyserial` is only needed for the serial simulator. `poll_test.py` uses the
pymodbus 3.15 client API (`device_id=`, not `slave=`).

## Testing ModbusSimTCP.py

1. Run the server. It listens on `0.0.0.0:5020`.

   ```bash
   python3 ModbusSimTCP.py
   ```

2. From another terminal, with the venv active, poll it:

   ```bash
   python3 poll_test.py            # defaults to 127.0.0.1
   python3 poll_test.py 192.168.1.42
   ```

   Or interactively with the pymodbus console:

   ```bash
   python3 -m pymodbus.console tcp --host 127.0.0.1 --port 5020
   ```

   ```text
   client.read_holding_registers address=0 count=2 slave=1
   ```

   Repeat for addresses 2 and 4.

3. To point firmware at it, the ESP32 and the host running the simulator must be
   on the same network, and the simulator's host address goes into
   `loadTcpBusConfig()`. That path lives on the `RS485` branch; see
   `lib/tcp_bus/` for the current state.

## Testing ModbusSimSerial.py

1. You need both ends of a serial link. Either a real RS485 or USB-serial adapter
   pair, or a virtual null modem:

   ```bash
   socat -d -d pty,raw,echo=0 pty,raw,echo=0
   ```

   This prints two linked `/dev/ttys00X` paths. Anything written to one appears
   on the other.

2. Edit `PORT` in `ModbusSimSerial.py` to the server's end of the link, then run
   it:

   ```bash
   python3 ModbusSimSerial.py
   ```

3. From another terminal, poll the other end:

   ```bash
   python3 -m pymodbus.console serial --port /dev/ttyXXXX --baudrate 9600 --framer rtu
   ```

   Read registers exactly as in the TCP test.

The server runs at 9600 baud `8N1`, which matches the `Rs485Config` defaults in
`loadModbusRtuConfig()`. If you change one, change the other, since the firmware
derives its T3.5 inter-frame gap from the serial format.

## Wiring the ESP32 to the serial simulator

To drive the real `Sp3485` path against this simulator rather than a meter, put a
USB-to-RS485 adapter on the host, set `PORT` to it, and wire A and B to the
transceiver on the node. The firmware side needs nothing changed: GPIO 8 RX,
GPIO 9 TX, GPIO 10 DE/RE, 9600 `8N1`, slave address 1, all already set in
`loadModbusRtuConfig()`.

## Notes

- `.venv/` should be ignored by git. The root `.gitignore` currently has
  `ModBusSim/.venv` with different capitalisation from this directory, which only
  matches because git is case-insensitive on macOS by default. On Linux or in CI
  the virtualenv would stop being ignored. `__pycache__/` is not ignored either.
- Both servers are `asyncio` based and stop with Ctrl-C.
