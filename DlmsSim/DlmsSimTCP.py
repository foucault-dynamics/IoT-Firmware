"""
Fake DLMS/COSEM meter over TCP, the DLMS version of ModbusSim/ModbusSimTCP.py.

Carries the raw HDLC frames over a socket instead of RS485, so the firmware's
DlmsCosemReader runs on a TcpBus unchanged (reader = 6, ReaderType::DlmsTcp).

    python3 DlmsSimTCP.py              # listens on 0.0.0.0:5022
    python3 DlmsSimTCP.py --port 6000
    python3 DlmsSimTCP.py --quiet      # sessions only, no hex frames

Port 5022, not 5020 or 5021, so it can run next to ModbusSimTCP.py and IrSimTCP.py.
One reader at a time, like the firmware's single TcpBus connection, and a
fresh meter per connection, so every connection starts from the same readings.
"""
import argparse
import logging
import socket

from dlms_meter import SimMeter

_logger = logging.getLogger(__name__)


def serve(conn: socket.socket):
    meter = SimMeter()
    while True:
        data = conn.recv(256)
        if not data:
            return
        reply = meter.feed(data)
        if reply:
            conn.sendall(reply)


def main():
    parser = argparse.ArgumentParser(description="Fake DLMS/COSEM meter over TCP")
    parser.add_argument("--port", type=int, default=5022)
    parser.add_argument("--quiet", action="store_true", help="don't log every frame in hex")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO if args.quiet else logging.DEBUG,
                        format="%(asctime)s %(message)s", datefmt="%H:%M:%S")

    server = socket.create_server(("0.0.0.0", args.port), reuse_port=True)
    _logger.info("fake DLMS meter listening on 0.0.0.0:%d (Ctrl-C to stop)", args.port)
    try:
        while True:
            conn, peer = server.accept()
            _logger.info("reader connected from %s", peer)
            with conn:
                try:
                    serve(conn)
                except ConnectionResetError:
                    pass
            _logger.info("reader %s disconnected", peer)
    except KeyboardInterrupt:
        pass
    finally:
        server.close()


if __name__ == "__main__":
    main()
