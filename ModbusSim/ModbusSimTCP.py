import logging
import asyncio
import sys
import time
import math
import struct

from pymodbus import ModbusDeviceIdentification
from pymodbus.constants import ExcCodes
from pymodbus.server import StartAsyncTcpServer
from pymodbus.simulator import DataType, SimData, SimDevice
from pymodbus import FramerType

# LOGGER
_logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO)

#REGISTER ADDRESS
VOLTAGE_ADDR = 0
KWH_EXPORT_ADDR = 2
KWH_IMPORT_ADDR = 4

def _encode_float32(value: float) -> list[int]:
    """Pack a float into 2 big-endian 16-bit registers."""
    raw = struct.pack(">f", value)
    return [int.from_bytes(raw[0:2], "big"), int.from_bytes(raw[2:4], "big")]
 
 
def _decode_uint32(regs: list[int]) -> int:
    """Unpack 2 big-endian 16-bit registers into an int."""
    raw = regs[0].to_bytes(2, "big") + regs[1].to_bytes(2, "big")
    return struct.unpack(">I", raw)[0]
 
 
def _encode_uint32(value: int) -> list[int]:
    """Pack an int into 2 big-endian 16-bit registers."""
    raw = struct.pack(">I", value)
    return [int.from_bytes(raw[0:2], "big"), int.from_bytes(raw[2:4], "big")]
 
 
async def meter_action(
    function_code: int,
    start_address: int,
    address: int,
    count: int,
    current_registers: list[int],
    set_values,
) -> ExcCodes | None:
    
    if set_values is not None:
        return None
 
    # Voltage: gently oscillating sine wave
    idx = VOLTAGE_ADDR - start_address
    if 0 <= idx < len(current_registers) - 1:
        voltage = 230.0 + 5.0 * math.sin(time.time() / 10)
        current_registers[idx : idx + 2] = _encode_float32(voltage)
 
    # kWh export: monotonically increasing counter
    idx = KWH_EXPORT_ADDR - start_address
    if 0 <= idx < len(current_registers) - 1:
        current = _decode_uint32(current_registers[idx : idx + 2])
        current_registers[idx : idx + 2] = _encode_uint32(current + 1)
 
    # kWh import: monotonically increasing counter
    idx = KWH_IMPORT_ADDR - start_address
    if 0 <= idx < len(current_registers) - 1:
        current = _decode_uint32(current_registers[idx : idx + 2])
        current_registers[idx : idx + 2] = _encode_uint32(current + 1)
 
    return None


def define_device() -> SimDevice:
    simdata = [
        SimData(VOLTAGE_ADDR,count=1,values=230.0,datatype=DataType.FLOAT32),
        SimData(KWH_EXPORT_ADDR,count=1,values=1000.0,datatype=DataType.FLOAT32),
        SimData(KWH_IMPORT_ADDR,count=1,values=100.0,datatype=DataType.FLOAT32)
    ]
    return SimDevice(id=1,simdata=simdata,action=meter_action)
    

async def run_async_server() -> None:
    _logger.info("## start Async server, listening on 5020 - TCP")

    # Address
    address = ("0.0.0.0",5020)
    # Identity
    identity = ModbusDeviceIdentification()
    # Device (Meter)
    device = define_device()

    await StartAsyncTcpServer(
        context = [device],
        identity=identity,
        address=address,
        framer=FramerType.RTU
    )

async def async_helper() -> None:
    _logger.info("Starting...")
    await run_async_server()


if __name__ == "__main__":
    asyncio.run(async_helper())
