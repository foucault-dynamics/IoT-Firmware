"""
Reads a meter the way the firmware's Iec6205621Reader does, from the laptop.
The IR version of ModbusSim/poll_test.py.

Over TCP it checks IrSimTCP.py with no hardware at all. Over serial it
can read IrSimSerial.py through a pair of adapters, or a real meter through
an optical probe -- handy for checking the PCB's readings against.

    python ir_poll_test.py                       # TCP, 127.0.0.1:5021, 3 reads
    python ir_poll_test.py --host 192.168.4.2 --count 5
    python ir_poll_test.py --serial COM6         # needs pyserial

Exit code 0 means every read worked.
"""
import argparse
import re
import socket
import sys
import time

from ir_meter import ACK, BAUD_FROM_ID, ETX, START_BAUD, STX, bcc, describe

TIMEOUT_S = 3.0


class TcpLink:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=TIMEOUT_S)

    def send(self, data):
        self.sock.sendall(data)

    def read_until(self, terminator):
        data = b""
        while not data.endswith(terminator):
            try:
                chunk = self.sock.recv(1)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
        return data

    def read_bytes(self, n):
        data = b""
        while len(data) < n:
            try:
                chunk = self.sock.recv(n - len(data))
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
        return data

    def set_baud(self, baud):
        pass  # no baud rate on a socket

    def close(self):
        self.sock.close()


class SerialLink:
    def __init__(self, port):
        import serial
        self.ser = serial.Serial(port, baudrate=START_BAUD, bytesize=serial.SEVENBITS,
                                 parity=serial.PARITY_EVEN, stopbits=serial.STOPBITS_ONE,
                                 timeout=TIMEOUT_S)

    def send(self, data):
        self.ser.reset_input_buffer()  # don't let leftovers from the last read desync this one
        self.ser.write(data)
        self.ser.flush()

    def read_until(self, terminator):
        return self.ser.read_until(terminator)

    def read_bytes(self, n):
        return self.ser.read(n)

    def set_baud(self, baud):
        self.ser.baudrate = baud

    def close(self):
        self.ser.close()


def read_once(link):
    """One full mode C read. Returns (import_kwh, export_kwh) or raises with what went wrong."""
    link.set_baud(START_BAUD)
    link.send(b"/?!\r\n")

    ident = link.read_until(b"\r\n")
    print(f"  ident: {describe(ident)}")
    if len(ident) < 7 or not ident.startswith(b"/"):
        raise RuntimeError("no identification message (meter didn't answer, or answered garbage)")
    baud_id = chr(ident[4])
    if baud_id not in BAUD_FROM_ID:
        raise RuntimeError(f"unknown baud-rate ID {baud_id!r}")

    link.send(bytes([ACK]) + f"0{baud_id}0\r\n".encode("ascii"))
    link.set_baud(BAUD_FROM_ID[baud_id])

    block = link.read_until(b"!\r\n")
    if block.startswith(bytes([STX])):
        tail = link.read_bytes(2)  # ETX + BCC
        if len(tail) != 2 or tail[0] != ETX:
            raise RuntimeError("data block not closed with ETX + BCC")
        expected = bcc(block[1:] + bytes([ETX]))
        if tail[1] != expected:
            raise RuntimeError(f"BCC mismatch: got {tail[1]:02X}, expected {expected:02X}")
        print("  frame: STX/ETX present, BCC ok")
    else:
        print("  frame: none (unframed data block)")

    text = block.decode("ascii", errors="replace")
    imp = re.search(r"1-0:1\.8\.0\(([\d.]+)", text)
    exp = re.search(r"1-0:2\.8\.0\(([\d.]+)", text)
    if not imp or not exp:
        raise RuntimeError(f"data block missing 1.8.0 or 2.8.0: {describe(block)}")
    return float(imp.group(1)), float(exp.group(1))


def main():
    parser = argparse.ArgumentParser(description="Read an IEC 62056-21 meter like the firmware does")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5021)
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
        try:
            imp, exp = read_once(link)
            print(f"  import = {imp:.3f} kWh   export = {exp:.3f} kWh")
            if last is not None and (imp, exp) == last:
                print("  WARNING: same as last read -- stale data?")
            last = (imp, exp)
        except RuntimeError as e:
            print(f"  FAILED: {e}")
            failures += 1
        if i < args.count:
            time.sleep(args.gap)

    link.close()
    print(f"\n{args.count - failures}/{args.count} reads ok")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
