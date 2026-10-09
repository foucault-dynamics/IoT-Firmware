"""
Fake IR meter over TCP, the IR version of ModbusSim/ModbusSimTCP.py.

Carries the raw IEC 62056-21 bytes over a socket instead of light. There is
no baud rate on a socket, so the baud switch is only logged.

    python IrSimTCP.py              # listens on 0.0.0.0:5021
    python IrSimTCP.py --port 6000
    python IrSimTCP.py --no-frame   # data block without STX/ETX/BCC

Port 5021, not 5020, so it can run next to ModbusSimTCP.py.
"""
import argparse
import asyncio
import logging

from ir_meter import FakeIrMeter, describe

_logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")

# A real meter gives up on a half-finished session after 1.5 s of silence.
IDLE_TIMEOUT_S = 1.5


async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter, framed: bool):
    peer = writer.get_extra_info("peername")
    _logger.info("reader connected from %s", peer)
    # One meter per connection, so every connection starts from the same readings.
    meter = FakeIrMeter(framed=framed)

    try:
        while True:
            try:
                message = await asyncio.wait_for(reader.readuntil(b"\n"), IDLE_TIMEOUT_S)
            except asyncio.TimeoutError:
                meter.reset()
                continue

            _logger.info("<- %s", describe(message))
            reply, new_baud = meter.handle(message)
            if reply is None:
                _logger.info("   (ignored, not a message the meter answers right now)")
                continue

            if new_baud is not None:
                _logger.info("   would switch to %d baud", new_baud)
            writer.write(reply)
            await writer.drain()
            _logger.info("-> %s", describe(reply))
    except (asyncio.IncompleteReadError, ConnectionResetError):
        pass
    finally:
        _logger.info("reader %s disconnected", peer)
        writer.close()


async def main():
    parser = argparse.ArgumentParser(description="Fake IEC 62056-21 meter over TCP")
    parser.add_argument("--port", type=int, default=5021)
    parser.add_argument("--no-frame", action="store_true",
                        help="send the data block without STX/ETX/BCC, like SimulatedIrHead")
    args = parser.parse_args()

    server = await asyncio.start_server(
        lambda r, w: handle_client(r, w, framed=not args.no_frame), "0.0.0.0", args.port)
    _logger.info("fake IR meter listening on 0.0.0.0:%d (Ctrl-C to stop)", args.port)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
