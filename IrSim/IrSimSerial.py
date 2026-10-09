"""
Fake IR meter on a serial port, the IR version of ModbusSim/ModbusSimSerial.py.

The closest thing to a real meter without one: real 300 baud 7E1 for the
handshake, a real switch to the negotiated baud for the data block, and back
to 300 afterwards. Wire a USB-to-serial adapter straight to the ESP32's IR
UART pins and RealIrHead runs unchanged -- the wire stands in for the light.

    python IrSimSerial.py COM5            # Windows
    python IrSimSerial.py /dev/ttyUSB0    # Linux / macOS
    python IrSimSerial.py COM5 --no-frame

Needs pyserial (pip install pyserial).
"""
import argparse
import logging
import time

import serial

from ir_meter import START_BAUD, FakeIrMeter, describe

_logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")

# A real meter gives up on a half-finished session after 1.5 s of silence.
IDLE_TIMEOUT_S = 1.5

# Pause between the reader's ACK and the data block. Real meters wait
# 200 ms - 1.5 s here, which is also what gives the reader time to retune
# its own UART after sending the ACK.
REACTION_TIME_S = 0.3


def main():
    parser = argparse.ArgumentParser(description="Fake IEC 62056-21 meter on a serial port")
    parser.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--no-frame", action="store_true",
                        help="send the data block without STX/ETX/BCC, like SimulatedIrHead")
    args = parser.parse_args()

    ser = serial.Serial(args.port, baudrate=START_BAUD, bytesize=serial.SEVENBITS,
                        parity=serial.PARITY_EVEN, stopbits=serial.STOPBITS_ONE,
                        timeout=IDLE_TIMEOUT_S)
    meter = FakeIrMeter(framed=not args.no_frame)
    _logger.info("fake IR meter on %s at %d baud 7E1 (Ctrl-C to stop)", args.port, START_BAUD)

    try:
        while True:
            message = ser.read_until(b"\n")
            if not message:
                meter.reset()
                continue
            if not message.endswith(b"\n"):
                # Half a message then silence: garbage, wrong baud, or a
                # reader that gave up. Start over like a real meter would.
                _logger.info("<- %s (incomplete, dropped)", describe(message))
                meter.reset()
                continue

            _logger.info("<- %s", describe(message))
            reply, new_baud = meter.handle(message)
            if reply is None:
                _logger.info("   (ignored, not a message the meter answers right now)")
                continue

            if new_baud is None:
                ser.write(reply)
                ser.flush()
                _logger.info("-> %s", describe(reply))
                continue

            time.sleep(REACTION_TIME_S)
            ser.baudrate = new_baud
            ser.write(reply)
            ser.flush()
            _logger.info("-> %s  (at %d baud)", describe(reply), new_baud)
            ser.baudrate = START_BAUD
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
