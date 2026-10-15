"""
The CV node's wired cam protocol, shared by CamSimSerial.py and cam_uart_poll.py.

Spec: docs/CV_WIRED_LINK.md. In short, the C3 sends one line, "GET <path>\\n",
and the cam answers STX (0x02), the body its HTTP server would send for that
path, then ETX (0x03). Anything outside STX and ETX is log noise and ignored.
"""
import json
import time

STX = b"\x02"
ETX = b"\x03"
MAX_BODY = 2048  # CAM_UART_MAX_BODY in uart_cam_bus.h

# The true values of AI-on-the-edge-device's 12 demo images, in files.txt order
DEMO_VALUES = [530.07077, 530.07325, 530.12067, 530.21419, 530.48435, 530.70265,
               530.95675, 531.10877, 531.24108, 531.38301, 531.63071, 531.82235]


def frame(body: bytes) -> bytes:
    """Wraps a reply body in STX and ETX."""
    return STX + body + ETX


def json_body(flows: dict) -> bytes:
    """Builds a /json reply shaped like AI-on-the-edge-device's, one object per flow."""
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    doc = {name: {"value": f"{v:.5f}" if v is not None else "", "raw": f"{v:011.5f}" if v is not None else "",
                  "pre": f"{v:.5f}" if v is not None else "", "error": "no error", "rate": "0.000000",
                  "timestamp": stamp}
           for name, v in flows.items()}
    return json.dumps(doc, indent=2).encode()


class FrameReader:
    """Picks one STX..ETX frame out of a byte stream, the same rules as CamUartFrame."""

    def __init__(self):
        self.buf = bytearray()
        self.in_frame = False
        self.noise = bytearray()

    def feed(self, data: bytes):
        """Feeds bytes; returns the body once a whole frame has arrived, else None."""
        for b in data:
            if b == STX[0]:
                self.buf.clear()
                self.in_frame = True
            elif not self.in_frame:
                self.noise.append(b)
            elif b == ETX[0]:
                self.in_frame = False
                return bytes(self.buf)
            elif len(self.buf) >= MAX_BODY:
                self.buf.clear()
                self.in_frame = False
            else:
                self.buf.append(b)
        return None
