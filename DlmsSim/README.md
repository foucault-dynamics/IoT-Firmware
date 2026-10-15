# DlmsSim

A Python stand-in for a DLMS/COSEM meter, so `DlmsCosemReader` in
`lib/protocols/dlms_cosem/` can be run end to end with no meter on the desk.

The QEMU suites check the reader against frames written for the tests, which
only shows the firmware agrees with its own reading of the standard. The meter
here is built on [Gurux.DLMS.Python](https://github.com/Gurux/Gurux.DLMS.Python),
an implementation written independently of the firmware, so a session that
works against it is evidence the two interoperate.

| File | What it is |
|---|---|
| `dlms_meter.py` | The meter, a Gurux `GXDLMSServer` with three Registers. Shared by both servers. |
| `DlmsSimTCP.py` | HDLC over TCP on `0.0.0.0:5022`. Firmware reaches it through `TcpBus` with `reader = 6`. |
| `DlmsSimSerial.py` | HDLC on a serial port at 9600 `8N1`. Firmware reaches it through the `Sp3485` with `reader = 5`. |
| `dlms_poll_test.py` | Laptop client using Gurux's `GXDLMSClient`. Reads the three Registers exactly as one firmware getter does. Use it to confirm the server before pointing firmware at it. |

## What the meter answers

Everything matches the defaults `rs485_loader.cpp` loads, so firmware with a
fresh NVS needs no DLMS keys set.

| Setting | Value |
|---|---|
| Link | HDLC, default parameters (128 byte info field, window 1) |
| Server address | logical 1, 1 byte (`dlms_logical = 1`, `dlms_addr_len = 1`) |
| Client | SAP 16, the public client (`dlms_client = 16`) |
| Association | LN referencing, no authentication, no ciphering |

A frame for any other server or client address gets no answer, which is how a
real meter behaves, so a wrong address in NVS shows up as a timeout. The sim
logs the address it ignored.

### Register map

| Value | OBIS | Type on the wire | Scaler | Unit | Behaviour on each read |
|---|---|---|---|---|---|
| Import | `1.0.1.8.0.255` | `double-long-unsigned` (uint32) | 0 | Wh (30) | Starts at 100000 Wh, grows by 100 Wh |
| Export | `1.0.2.8.0.255` | `double-long-unsigned` (uint32) | 0 | Wh (30) | Starts at 1000000 Wh, grows by 100 Wh |
| Voltage | `1.0.32.7.0.255` | `long-unsigned` (uint16) | -1 | V (35) | `230 + 5*sin(t/10)` V, sent in tenths, so 2302 reads as 230.2 V |

The integer types and the voltage scaler are deliberate. A real meter rarely
sends plain floats, so this exercises the firmware's A-XDR integer decoding and
its scaling. The firmware divides import and export by 1000, so expect 100.1 kWh
and 1000.1 kWh on the node's serial.

A value changes on every read of it, like ModbusSim's, so a stale or cached
reading is obvious. Each TCP connection gets a fresh meter, so a reconnect
starts the counters over.

## Workarounds for Gurux bugs

The server side of gurux-dlms 1.0.203, the current release, has bugs that stop
it answering at all. Upstream master has the same code. `dlms_meter.py` works
around each one at the point it bites, with a comment saying which:

- `GXServerReply` lacks `setReply()`, `setCount()` and `getConnectionInfo()`,
  which `handleRequest()` calls. Covered by a small subclass.
- `initialize()` appends the whole object collection to the default
  association as if it were one object. Covered by supplying our own Association
  LN.
- `handleSnrmRequest()` and `generateDisconnectRequest()` read the negotiated
  link values (`maxInfoTX` and so on) off `self.hdlc`, the IEC HDLC setup object
  that holds the meter's limits under other names, instead of
  `self.settings.hdlc`. They also call a nonexistent `update()`, and write the
  parameter group length with `setUInt8`'s arguments swapped. Covered by
  overriding both with the same steps on the right objects.
- `handleRequest()` measures the inactivity timeout with `int()` of a
  `timedelta`. Covered by setting the timeout to 0.
- GET calls `notifyRead()`, which `GXDLMSServer` doesn't define. Covered by a
  no-op.

The workarounds lean on Gurux internals, so pin the version. All framing, AARE
encoding, GET response encoding and A-XDR typing is still Gurux's own code.

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install gurux-dlms==1.0.203 pyserial
```

`pyserial` is only needed for the serial simulator and `dlms_poll_test.py --serial`.

## Testing DlmsSimTCP.py

1. Run the server. It listens on `0.0.0.0:5022` and logs every frame in hex
   (`--quiet` for sessions only).

   ```bash
   python3 DlmsSimTCP.py
   ```

2. From another terminal, with the venv active, poll it:

   ```bash
   python3 dlms_poll_test.py                  # 127.0.0.1, 3 reads
   python3 dlms_poll_test.py --host 192.168.4.2 --count 5
   ```

   Expect three values per read, in Wh and V, and different values each read.

3. To point firmware at it, set NVS `reader = 6`, `tcp_port = 5022`, and
   `tcp_host` to the laptop's address on the node's softAP. Join the laptop to
   the softAP (`ap_ssid`, `ap_pass`) and run the server. Each poll, the node
   prints import, export and voltage, and the sim logs SNRM, AARQ, two GETs and
   DISC for each Register. A failed session can be diffed frame by frame against
   the firmware's serial log.

## Testing DlmsSimSerial.py

1. You need both ends of a serial link. Either a real USB-to-RS485 adapter pair,
   or a virtual null modem:

   ```bash
   socat -d -d pty,raw,echo=0 pty,raw,echo=0
   ```

   This prints two linked `/dev/ttys00X` paths.

2. Run the server on one end. The port defaults to `/dev/cu.wchusbserial10`,
   the same as `ModbusSimSerial.py`.

   ```bash
   python3 DlmsSimSerial.py /dev/ttys003
   ```

3. Poll the other end:

   ```bash
   python3 dlms_poll_test.py --serial /dev/ttys004
   ```

## Wiring the ESP32 to the serial simulator

Put a USB-to-RS485 adapter on the laptop, run `DlmsSimSerial.py` on it, and
wire A and B to the node's SP3485. Set NVS `reader = 5`. The rest is already
the `loadRs485NodeConfig()` default: GPIO 8 RX, GPIO 9 TX, GPIO 10 DE/RE,
9600 `8N1`, and the DLMS addresses above.

## Credits

The meter and the poll test are built on
[Gurux.DLMS.Python](https://github.com/Gurux/Gurux.DLMS.Python) by Gurux Ltd,
licensed under GPLv2. `GXDLMSServer` provides the meter, `GXDLMSClient` drives
`dlms_poll_test.py`. Developed against gurux-dlms 1.0.203. DlmsSim is a
standalone test tool and is not linked into the firmware.

Serial port access for `DlmsSimSerial.py` and `dlms_poll_test.py` comes from
[pySerial](https://github.com/pyserial/pyserial) by Chris Liechti, licensed
under BSD-3-Clause.
