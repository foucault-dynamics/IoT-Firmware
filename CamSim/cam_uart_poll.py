"""
Polls a cam over its UART the way the C3 does, without the C3.

Use it to bring up the cam side of the wired link: wire a USB to serial adapter
to the cam (adapter TX to cam RX, adapter RX to cam TX, GND to GND), then

    python cam_uart_poll.py /dev/ttyUSB0
    python cam_uart_poll.py /dev/ttyUSB0 --count 0 --every 30   # keep polling
    python cam_uart_poll.py /dev/ttyUSB0 --show-noise           # print the log text it skipped

The ESP32-CAM-MB programmer board already wires its CH340 to the cam's UART, so
with the cam on that board no extra wiring is needed. Its CH340 replays stale
bytes for a few seconds after the port opens, which --settle (default 5 s)
waits out.

Also works against CamSimSerial.py through two adapters, or a socat pty pair.
Needs pyserial (pip install pyserial).
"""
import argparse
import json
import time

import serial

from cam_protocol import FrameReader


def poll(port, path, timeout):
    """Sends one request; returns (body or None, skipped noise bytes, seconds taken)."""
    port.reset_input_buffer()
    port.write(f"GET {path}\n".encode())
    port.flush()
    reader = FrameReader()
    t0 = time.time()
    while time.time() - t0 < timeout:
        body = reader.feed(port.read(256))
        if body is not None:
            return body, bytes(reader.noise), time.time() - t0
    return None, bytes(reader.noise), time.time() - t0


def main():
    p = argparse.ArgumentParser(description="Poll a cam's wired /json the way the CV node does")
    p.add_argument("port", help="serial port, e.g. /dev/ttyUSB0 or COM5")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--path", default="/json")
    p.add_argument("--timeout", type=float, default=2.0, help="seconds to wait for a frame (the C3 waits 2)")
    p.add_argument("--count", type=int, default=1, help="requests to send, 0 for forever")
    p.add_argument("--every", type=float, default=5, help="seconds between requests")
    p.add_argument("--show-noise", action="store_true", help="print the bytes skipped outside the frame")
    p.add_argument("--settle", type=float, default=5, help="seconds to discard input after opening (default 5)")
    a = p.parse_args()

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = a.port, a.baud, 0.05
    # Released before opening. On an ESP32-CAM-MB, DTR pulls the cam's IO0 low (a reset
    # then lands in download mode, and IO0 is the camera's clock pin) and RTS drives EN.
    port.dtr = False
    port.rts = False
    port.open()
    end = time.time() + a.settle
    while time.time() < end:
        port.read(4096)
    n = 0
    while a.count == 0 or n < a.count:
        n += 1
        body, noise, took = poll(port, a.path, a.timeout)
        if body is None:
            print(f"#{n}: no frame within {a.timeout:.1f} s ({len(noise)} bytes of other output seen)")
        else:
            try:
                doc = json.loads(body)
                summary = ", ".join(f"{k}={v.get('value')!r} ({v.get('error')})" for k, v in doc.items())
                print(f"#{n}: {len(body)} byte frame in {took * 1000:.0f} ms: {summary or 'no flows'}")
            except (ValueError, AttributeError) as e:
                print(f"#{n}: {len(body)} byte frame in {took * 1000:.0f} ms, not valid /json: {e}: {body[:80]!r}")
        if a.show_noise and noise:
            print("   skipped:", noise.decode(errors="replace").strip()[:300])
        if a.count == 0 or n < a.count:
            time.sleep(a.every)


if __name__ == "__main__":
    main()
