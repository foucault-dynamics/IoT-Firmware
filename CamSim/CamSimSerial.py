"""
Fake ESP32-CAM on a serial port, for testing the CV node's wired link (cam_link 1).

Answers "GET /json" with a framed AI-on-the-edge-device style reply, stepping
through the demo meter's true values, one per --step seconds. Wire a USB to
serial adapter to the C3: adapter TX to the C3's cam RX pin (cam_rx, GPIO 20 by
default), adapter RX to cam_tx (GPIO 21), and GND to GND.

    python CamSimSerial.py /dev/ttyUSB0
    python CamSimSerial.py /dev/ttyUSB0 --export export   # also serve an export flow
    python CamSimSerial.py /dev/ttyUSB0 --noise           # print cam-style log lines too
    python CamSimSerial.py /dev/ttyUSB0 --fail missing    # misbehave, see --help

Needs pyserial (pip install pyserial).
"""
import argparse
import logging
import time

import serial

from cam_protocol import DEMO_VALUES, frame, json_body

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
log = logging.getLogger(__name__)


def main():
    p = argparse.ArgumentParser(description="Fake AI-on-the-edge-device cam on a serial port")
    p.add_argument("port", help="serial port, e.g. /dev/ttyUSB0 or COM5")
    p.add_argument("--baud", type=int, default=115200, help="must match the C3's cam_baud (default 115200)")
    p.add_argument("--flow", default="main", help="import flow name, the C3's cam_flow (default main)")
    p.add_argument("--export", default="", help="also serve an export flow with this name")
    p.add_argument("--step", type=float, default=60, help="seconds per demo value (default 60, like the cam)")
    p.add_argument("--noise", action="store_true", help="print a cam-style log line every second")
    p.add_argument("--fail", choices=["none", "silent", "missing", "novalue", "noetx"], default="none",
                   help="silent: never answer. missing: leave the flow out. novalue: empty value, as "
                        "just after boot. noetx: cut the frame off before ETX")
    a = p.parse_args()

    port = serial.Serial(a.port, a.baud, timeout=0.05)
    log.info("CamSim on %s at %d baud, flow %r%s, mode %s", a.port, a.baud, a.flow,
             f", export {a.export!r}" if a.export else "", a.fail)
    start = time.time()
    line = bytearray()
    last_noise = 0.0
    while True:
        data = port.read(256)
        now = time.time()
        if a.noise and now - last_noise >= 1:
            port.write(f"I ({int((now - start) * 1000)}) MAINCTRL: Round running\r\n".encode())
            last_noise = now
        for b in data:
            if b in (0x0A, 0x0D):
                if line:
                    handle(port, line.decode(errors="replace").strip(), a, now - start)
                line.clear()
            else:
                line.append(b)


def handle(port, request, a, elapsed):
    if not request.startswith("GET "):
        log.info("ignored %r", request)
        return
    path = request[4:]
    value = DEMO_VALUES[int(elapsed // a.step) % len(DEMO_VALUES)]
    log.info("%s -> %s %.5f", request, a.fail if a.fail != "none" else "reply", value)
    if a.fail == "silent":
        return
    if path.split("?")[0] != "/json":
        port.write(frame(b""))  # unknown path: empty body, like a 404
        return
    flows = {} if a.fail == "missing" else {a.flow: None if a.fail == "novalue" else value}
    if a.export and a.fail != "missing":
        flows[a.export] = round(value * 0.1, 5)
    reply = frame(json_body(flows))
    port.write(reply[:-1] if a.fail == "noetx" else reply)


if __name__ == "__main__":
    main()
