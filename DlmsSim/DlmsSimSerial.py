"""
Fake DLMS/COSEM meter on a serial port, the DLMS version of
ModbusSim/ModbusSimSerial.py.

9600 8N1, the Rs485Config defaults rs485_loader.cpp gives the DLMS reader.
Put a USB-to-RS485 adapter on this port and wire A and B to the node's SP3485,
and DlmsCosemReader runs over the real transceiver path (reader = 5).

    python3 DlmsSimSerial.py                  # /dev/cu.wchusbserial10
    python3 DlmsSimSerial.py /dev/ttyUSB0
    python3 DlmsSimSerial.py COM5 --quiet     # sessions only, no hex frames

Needs pyserial (pip install pyserial).
"""
import argparse
import logging

import serial

from dlms_meter import SimMeter

_logger = logging.getLogger(__name__)

BAUD = 9600


def main():
    parser = argparse.ArgumentParser(description="Fake DLMS/COSEM meter on a serial port")
    parser.add_argument("port", nargs="?", default="/dev/cu.wchusbserial10",
                        help="serial port, e.g. /dev/ttyUSB0 or COM5")
    parser.add_argument("--quiet", action="store_true", help="don't log every frame in hex")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO if args.quiet else logging.DEBUG,
                        format="%(asctime)s %(message)s", datefmt="%H:%M:%S")

    ser = serial.Serial(args.port, baudrate=BAUD, bytesize=serial.EIGHTBITS,
                        parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE, timeout=0.1)
    meter = SimMeter()
    _logger.info("fake DLMS meter on %s at %d baud 8N1 (Ctrl-C to stop)", args.port, BAUD)

    try:
        while True:
            data = ser.read(ser.in_waiting or 1)
            if not data:
                continue
            reply = meter.feed(data)
            if reply:
                ser.write(reply)
                ser.flush()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
