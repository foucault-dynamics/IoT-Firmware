"""Minimal RTU-over-TCP client matching ModbusSimTCP.py's framing."""
import struct
import sys

from pymodbus import FramerType
from pymodbus.client import ModbusTcpClient

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = 5020

# Must match ModbusSimTCP.py:19-21
REGISTERS = {"voltage": 0, "kwh_export": 2, "kwh_import": 4}


def decode_f32(regs):
    """Two big-endian 16-bit registers -> float. Mirrors ModbusSimTCP.py:29-32."""
    raw = regs[0].to_bytes(2, "big") + regs[1].to_bytes(2, "big")
    return struct.unpack(">f", raw)[0]


client = ModbusTcpClient(HOST, port=PORT, framer=FramerType.RTU, timeout=3)
if not client.connect():
    sys.exit(f"could not connect to {HOST}:{PORT}")

for name, addr in REGISTERS.items():
    # device_id, not slave -- pymodbus 3.15 API. Server is SimDevice(id=1).
    rr = client.read_holding_registers(addr, count=2, device_id=1)
    if rr.isError():
        print(f"{name:12} ERROR {rr}")
    else:
        print(f"{name:12} {decode_f32(rr.registers):.3f}")

client.close()
