/**
 * @file
 * Fake IEC 62056-21 mode C meter on an ESP32 board with an IR head.
 *
 * The C version of IrSim/ir_meter.py, for testing the IR node over real
 * light: point this board's IR head at the IR node's head and the node reads
 * it like a meter.
 *
 *   reader -> "/?!\r\n"                    request, 300 baud
 *   meter  -> "/EMH5EM211\r\n"             identification ('5' = offers 9600 baud)
 *   reader -> ACK "0" "5" "0" "\r\n"       acknowledge, switch to 9600
 *   meter  -> STX <data block> "!\r\n" ETX BCC, then back to 300
 *
 * Import and export go up on every read, so a stale reading is obvious.
 *
 * Type "t" or "r" in the serial monitor to flip TX or RX inversion without
 * reflashing, for finding which way round the IR circuit is.
 */

#include <Arduino.h>

namespace {

/** UART RX GPIO, the head's RX (phototransistor). Set per board in platformio.ini. */
constexpr int RX_PIN = METER_RX_PIN;
/** UART TX GPIO, the head's TX (IR LED). Set per board in platformio.ini. */
constexpr int TX_PIN = METER_TX_PIN;

/** Rate every mode C session opens at, fixed by the standard. */
constexpr uint32_t START_BAUD = 300;
/** Baud rate ID offered in the identification message, '5' = 9600. */
constexpr char BAUD_ID = '5';

/**
 * How long the meter waits after the ACK before sending at the new rate.
 * The standard allows 200 to 1500 ms. It also gives the reader time to retune.
 */
constexpr unsigned long REACTION_MS = 300;
/** A half-finished session is dropped after this long without the ACK. */
constexpr unsigned long ACK_TIMEOUT_MS = 2000;
/** Bytes with no CR LF after this long are noise and get thrown away. */
constexpr unsigned long LINE_TIMEOUT_MS = 1500;

/** kWh added to import per read, same as ir_meter.py. */
constexpr double IMPORT_STEP = 0.1;
/** kWh added to export per read, same as ir_meter.py. */
constexpr double EXPORT_STEP = 0.05;

constexpr char STX = 0x02;  ///< Start of text, opens the data block.
constexpr char ETX = 0x03;  ///< End of text, closes the data block before the BCC.
constexpr char ACK = 0x06;  ///< First byte of the reader's acknowledgement.

/**
 * Same as the IR node's ir_tx_inv default: off for the PiggyMeter head, whose
 * LED lights on LOW, so the LED idles off. Both boards should match.
 */
bool txInvert = false;
/** Same as the IR node's ir_rx_inv default: off, the head's RX already idles HIGH. */
bool rxInvert = false;

double importKwh = 1234.567;  ///< Import counter, 1.8.0.
double exportKwh = 45.123;    ///< Export counter, 2.8.0.
uint32_t reads = 0;           ///< Data blocks sent since boot.

bool awaitingAck = false;      ///< True between the identification and the ACK.
unsigned long idSentAt = 0;    ///< millis() when the identification went out.
String line;                   ///< Bytes received since the last LF.
unsigned long lastByteAt = 0;  ///< millis() of the last byte received.

HardwareSerial &irPort = Serial1;  ///< UART on the IR head.

/** Start or restart the IR UART at a rate. 7E1 throughout, as the standard requires. */
void beginIr(uint32_t baud) {
  irPort.end();
  // begin() flips RX and TX together, so it sets TX and setRxInvert() sets RX.
  irPort.begin(baud, SERIAL_7E1, RX_PIN, TX_PIN, txInvert);
  irPort.setRxInvert(rxInvert);
}

/** Bytes as readable text for the log, control characters spelled out. */
String describe(const String &data) {
  String out;
  for (unsigned int i = 0; i < data.length(); i++) {
    uint8_t b = (uint8_t)data[i];
    if (b == STX) out += "<STX>";
    else if (b == ETX) out += "<ETX>";
    else if (b == ACK) out += "<ACK>";
    else if (b == '\r') out += "\\r";
    else if (b == '\n') out += "\\n";
    else if (b >= 32 && b < 127) out += (char)b;
    else {
      char hex[6];
      snprintf(hex, sizeof(hex), "<%02X>", b);
      out += hex;
    }
  }
  return out;
}

/** Write to the IR head and wait until the last bit has left. */
void sendIr(const String &data) {
  irPort.write((const uint8_t *)data.c_str(), data.length());
  irPort.flush();
  Serial.printf("-> %s\n", describe(data).c_str());
}

/** The next data block, framed with STX, ETX and BCC. Counters go up first. */
String dataBlock() {
  importKwh += IMPORT_STEP;
  exportKwh += EXPORT_STEP;
  reads++;

  char body[128];
  snprintf(body, sizeof(body),
           "0-0:96.1.0(12345678)\r\n"
           "1-0:1.8.0(%010.3f*kWh)\r\n"
           "1-0:2.8.0(%010.3f*kWh)\r\n"
           "!\r\n",
           importKwh, exportKwh);

  // BCC: XOR of every byte after STX, up to and including ETX.
  String afterStx = String(body) + ETX;
  uint8_t bcc = 0;
  for (unsigned int i = 0; i < afterStx.length(); i++) bcc ^= (uint8_t)afterStx[i];

  return String(STX) + afterStx + (char)bcc;
}

/** Answer one CR LF terminated message from the reader. */
void handleLine(const String &msg) {
  Serial.printf("<- %s\n", describe(msg).c_str());

  // A request restarts the session from any state. "/?!" or "/?<address>!".
  if (msg.startsWith("/?") && msg.endsWith("!\r\n")) {
    sendIr(String("/EMH") + BAUD_ID + "EM211\r\n");
    awaitingAck = true;
    idSentAt = millis();
    return;
  }

  if (awaitingAck && msg.length() >= 3 && msg[0] == ACK) {
    awaitingAck = false;
    char mode = msg[1];
    char baudId = msg[2];
    if (mode != '0' || baudId < '0' || baudId > '6') {
      Serial.println("   ACK asks for something this fake doesn't do, ignoring");
      return;
    }

    static const uint32_t rates[] = {300, 600, 1200, 2400, 4800, 9600, 19200};
    uint32_t baud = rates[baudId - '0'];

    beginIr(baud);
    delay(REACTION_MS);
    Serial.printf("   at %lu baud, read #%lu\n", (unsigned long)baud, (unsigned long)(reads + 1));
    sendIr(dataBlock());

    // Back to idle. Restarting the UART also drops this board's own echo.
    beginIr(START_BAUD);
    line = "";
    return;
  }

  // Usually this board's own identification seen by its own phototransistor.
  Serial.println("   (ignored)");
}

/** "t" or "r" in the serial monitor flips TX or RX inversion. */
void handleConsole() {
  while (Serial.available()) {
    char c = tolower(Serial.read());
    if (c != 't' && c != 'r') continue;
    if (c == 't') txInvert = !txInvert;
    else rxInvert = !rxInvert;
    beginIr(START_BAUD);
    line = "";
    awaitingAck = false;
    Serial.printf("TX invert %s, RX invert %s\n", txInvert ? "ON" : "OFF", rxInvert ? "ON" : "OFF");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);  // give the USB serial monitor time to attach

  beginIr(START_BAUD);
  Serial.println("\nFake IR meter (IEC 62056-21 mode C)");
  Serial.printf("RX=%d TX=%d, 300 baud 7E1, TX invert %s, RX invert %s.\n",
                RX_PIN, TX_PIN, txInvert ? "ON" : "OFF", rxInvert ? "ON" : "OFF");
  Serial.println("Type t or r to flip TX or RX invert.");
  Serial.println("Waiting for /?! from the IR node...");
}

void loop() {
  handleConsole();

  while (irPort.available()) {
    char c = irPort.read();
    lastByteAt = millis();
    line += c;
    if (c == '\n') {
      handleLine(line);
      line = "";
    } else if (line.length() > 80) {
      Serial.printf("   noise: %s\n", describe(line).c_str());
      line = "";
    }
  }

  // Bytes but never a line ending: usually the inversion is the wrong way round.
  if (line.length() && millis() - lastByteAt > LINE_TIMEOUT_MS) {
    Serial.printf("   noise, no line ending (try t or r?): %s\n", describe(line).c_str());
    line = "";
  }

  if (awaitingAck && millis() - idSentAt > ACK_TIMEOUT_MS) {
    Serial.println("   no ACK, back to idle");
    awaitingAck = false;
  }
}
