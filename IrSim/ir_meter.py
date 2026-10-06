"""
Fake IEC 62056-21 mode C meter. The protocol logic shared by IrSimTCP.py and
IrSimSerial.py: bytes in from the reader, bytes out from the "meter".

It answers the same way a real optical-port meter does:

  reader -> "/?!\r\n"                    request
  meter  -> "/EMH5EM211\r\n"             identification ('5' = offers 9600 baud)
  reader -> ACK "0" "5" "0" "\r\n"       acknowledge, switch to 9600
  meter  -> STX <data block> "!\r\n" ETX BCC

The energy counters go up on every read, like ModbusSim's, so a stale or
cached reading in the firmware is obvious.
"""

STX = 0x02
ETX = 0x03
ACK = 0x06

# IEC 62056-21 Table 6, mode C. Same mapping as Iec6205621Reader::baudRateFromId().
BAUD_FROM_ID = {"0": 300, "1": 600, "2": 1200, "3": 2400, "4": 4800, "5": 9600, "6": 19200}

# Every mode C meter listens at 300 baud when idle.
START_BAUD = 300

IMPORT_STEP = 0.1   # kWh added to import per read
EXPORT_STEP = 0.05  # kWh added to export per read


def bcc(data: bytes) -> int:
    """Block check character: XOR of every byte after STX, up to and including ETX."""
    result = 0
    for b in data:
        result ^= b
    return result


class FakeIrMeter:
    def __init__(self, manufacturer="EMH", baud_id="5", ident="EM211",
                 import_kwh=1234.567, export_kwh=45.123, framed=True):
        # framed=False drops STX/ETX/BCC so the data block matches
        # SimulatedIrHead's byte for byte.
        self.manufacturer = manufacturer
        self.baud_id = baud_id
        self.ident = ident
        self.import_kwh = import_kwh
        self.export_kwh = export_kwh
        self.framed = framed
        self.awaiting_ack = False
        self.reads = 0

    def reset(self):
        """Back to idle, as a real meter does after a data block or a timeout."""
        self.awaiting_ack = False

    def identification(self) -> bytes:
        return f"/{self.manufacturer}{self.baud_id}{self.ident}\r\n".encode("ascii")

    def data_block(self) -> bytes:
        self.import_kwh += IMPORT_STEP
        self.export_kwh += EXPORT_STEP
        self.reads += 1

        body = (
            "0-0:96.1.0(12345678)\r\n"
            f"1-0:1.8.0({self.import_kwh:010.3f}*kWh)\r\n"
            f"1-0:2.8.0({self.export_kwh:010.3f}*kWh)\r\n"
            "!\r\n"
        ).encode("ascii")

        if not self.framed:
            return body
        after_stx = body + bytes([ETX])
        return bytes([STX]) + after_stx + bytes([bcc(after_stx)])

    def handle(self, message: bytes):
        """
        Takes one CRLF-terminated message from the reader. Returns
        (reply, new_baud): the bytes the meter sends back (or None to stay
        quiet), and the baud rate to switch to before sending them (or None
        to stay where it is).
        """
        # A request restarts the session from any state, as on a real meter.
        # "/?!" or "/?<address>!" both count.
        if message.startswith(b"/?") and message.endswith(b"!\r\n"):
            self.awaiting_ack = True
            return self.identification(), None

        if self.awaiting_ack and len(message) >= 3 and message[0] == ACK:
            self.awaiting_ack = False
            mode = chr(message[1])
            baud_id = chr(message[2])
            if mode != "0":
                # '1' = programming mode, which this fake doesn't do.
                return None, None
            if baud_id not in BAUD_FROM_ID:
                return None, None
            return self.data_block(), BAUD_FROM_ID[baud_id]

        return None, None


def describe(data: bytes) -> str:
    """Bytes as readable text for logs: control characters spelled out."""
    names = {STX: "<STX>", ETX: "<ETX>", ACK: "<ACK>", 0x0D: "\\r", 0x0A: "\\n"}
    return "".join(names.get(b, chr(b) if 32 <= b < 127 else f"<{b:02X}>") for b in data)
