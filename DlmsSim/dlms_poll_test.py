"""
Reads a meter the way the firmware's DlmsCosemReader does, from the laptop.
The DLMS version of ModbusSim/poll_test.py.

The client is Gurux's GXDLMSClient, not a copy of the firmware, so this also
checks the sim's frames against a second implementation. Each Register gets
one whole session, exactly like one firmware getter:

    SNRM -> UA, AARQ -> AARE, GET scaler_unit, GET value, DISC -> UA

    python3 dlms_poll_test.py                       # TCP, 127.0.0.1:5022, 3 reads
    python3 dlms_poll_test.py --host 192.168.4.2 --count 5
    python3 dlms_poll_test.py --serial /dev/ttys004 # needs pyserial

Exit code 0 means every read worked.
"""
import argparse
import socket
import sys
import time

from gurux_dlms import GXDLMSClient, GXReplyData
from gurux_dlms.enums import Authentication, InterfaceType
from gurux_dlms.objects import GXDLMSRegister

from dlms_meter import CLIENT_SAP, EXPORT_OBIS, IMPORT_OBIS, SERVER_LOGICAL, VOLTAGE_OBIS

TIMEOUT_S = 3.0
HDLC_FLAG = 0x7E

REGISTERS = (("import", IMPORT_OBIS), ("export", EXPORT_OBIS), ("voltage", VOLTAGE_OBIS))
UNITS = {30: "Wh", 35: "V"}


class TcpLink:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=TIMEOUT_S)

    def send(self, data):
        self.sock.sendall(data)

    def read(self, n):
        try:
            return self.sock.recv(n)
        except socket.timeout:
            return b""

    def close(self):
        self.sock.close()


class SerialLink:
    def __init__(self, port):
        import serial
        self.ser = serial.Serial(port, baudrate=9600, bytesize=serial.EIGHTBITS,
                                 parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                 timeout=TIMEOUT_S)

    def send(self, data):
        self.ser.write(data)
        self.ser.flush()

    def read(self, n):
        return self.ser.read(n)

    def close(self):
        self.ser.close()


def read_exact(link, n):
    data = b""
    while len(data) < n:
        chunk = link.read(n - len(data))
        if not chunk:
            raise RuntimeError(f"timed out after {len(data)} of {n} bytes")
        data += chunk
    return data


def read_frame(link):
    """One HDLC frame: opening flag, then the 11 bit length in the format field says how much follows."""
    flag = read_exact(link, 1)
    if flag[0] != HDLC_FLAG:
        raise RuntimeError(f"expected 7E, got {flag.hex()}")
    fmt = read_exact(link, 2)
    length = ((fmt[0] & 0x07) << 8) | fmt[1]
    rest = read_exact(link, length - 2 + 1)  # length counts the format field, plus the closing flag
    return flag + fmt + rest


def transact(client, link, frames):
    """Sends each frame and returns the parsed reply to the last one."""
    if not isinstance(frames, list):
        frames = [frames]
    reply = None
    for frame in frames:
        link.send(bytes(frame))
        reply = GXReplyData()
        client.getData(bytearray(read_frame(link)), reply)
    return reply


def read_register(link, obis):
    """One firmware-style session for one Register. Returns (value, unit code)."""
    client = GXDLMSClient(True, CLIENT_SAP, SERVER_LOGICAL, Authentication.NONE, None, InterfaceType.HDLC)
    reg = GXDLMSRegister(obis)
    client.parseUAResponse(transact(client, link, client.snrmRequest()).data)
    client.parseAareResponse(transact(client, link, client.aarqRequest()).data)
    client.updateValue(reg, 3, transact(client, link, client.read(reg, 3)).value)
    client.updateValue(reg, 2, transact(client, link, client.read(reg, 2)).value)
    transact(client, link, client.disconnectRequest())
    # Gurux applies the scaler as a float multiply, so 2302 * 0.1 needs rounding back.
    return round(reg.value, 6), int(reg.unit)


def main():
    parser = argparse.ArgumentParser(description="Read a DLMS/COSEM meter like the firmware does")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5022)
    parser.add_argument("--serial", metavar="PORT", help="read over a serial port instead of TCP")
    parser.add_argument("--count", type=int, default=3, help="how many reads (default 3)")
    parser.add_argument("--gap", type=float, default=2.0, help="seconds between reads (default 2)")
    args = parser.parse_args()

    try:
        link = SerialLink(args.serial) if args.serial else TcpLink(args.host, args.port)
    except OSError as e:
        sys.exit(f"could not connect: {e}")

    failures = 0
    last = None
    for i in range(1, args.count + 1):
        print(f"read {i}:")
        values = []
        for name, obis in REGISTERS:
            try:
                value, unit = read_register(link, obis)
                print(f"  {name:8} {obis:15} = {value} {UNITS.get(unit, f'unit {unit}')}")
                values.append(value)
            except Exception as e:
                print(f"  {name:8} {obis:15} FAILED: {e}")
                failures += 1
        if last is not None and values == last:
            print("  WARNING: same as last read -- stale data?")
        last = values
        if i < args.count:
            time.sleep(args.gap)

    link.close()
    total = args.count * len(REGISTERS)
    print(f"\n{total - failures}/{total} register reads ok")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
