#include "iec62056_21.h"
#include <Arduino.h>
#include <cstdlib>

namespace {
constexpr unsigned long ID_TIMEOUT_MS = 2000;
constexpr unsigned long DATA_TIMEOUT_MS = 3000;
}  // namespace

Iec6205621Reader::Iec6205621Reader(const Iec62056Config &config) : config(config) {}

int Iec6205621Reader::init(Module &module) {
  // Safe only because every call site wires an IrHead (RealIrHead or
  // SimulatedIrHead) here -- this reader is IEC 62056-21 specific and the
  // optical port is the only thing it's ever attached to.
  head = static_cast<IrHead *>(&module);
  head->init();
  return EXIT_SUCCESS;
}

String Iec6205621Reader::readUntil(const char *terminator,
                                    unsigned long timeoutMs) {
  String result;
  unsigned long deadline = millis() + timeoutMs;

  while (millis() < deadline) {
    int b = head->readByte();
    if (b != -1) {
      result += (char)b;
      if (result.endsWith(terminator)) break;
    }
  }
  return result;
}

bool Iec6205621Reader::baudRateFromId(char code, uint32_t &baudOut) {
  switch (code) {
    case '0': baudOut = 300;   return true;
    case '1': baudOut = 600;   return true;
    case '2': baudOut = 1200;  return true;
    case '3': baudOut = 2400;  return true;
    case '4': baudOut = 4800;  return true;
    case '5': baudOut = 9600;  return true;
    case '6': baudOut = 19200; return true;
    default:  return false;  // meter asked for a rate outside the table
  }
}

int Iec6205621Reader::handshake(uint32_t &negotiatedBaud) {
  // Step 1: the request message. "/" marks it as a request, "?" means
  // "send identification", "!" is a fixed terminator the spec requires,
  // CRLF ends the line. Sent at 300 baud -- the one speed every mode C
  // meter is guaranteed to be listening at when idle, regardless of what
  // higher speeds it supports for the data block.
  const uint8_t request[] = {'/', '?', '!', '\r', '\n'};
  head->send(request, sizeof(request));

  // Step 2: the identification response. Shape per IEC 62056-21:
  //   "/" + 3-char manufacturer ID + 1-char baud-rate ID + identification
  //   text + CR LF
  // We only need bytes 0-4 (the "/", the 3 manufacturer chars, and the
  // baud-rate ID) -- the identification text past that is metadata we
  // don't need for the handshake itself.
  String identification = readUntil("\r\n", ID_TIMEOUT_MS);
  if (identification.length() == 0) {
    Serial.println("[IEC62056-21] meter never responded to request message");
    return -1;  // meter never answered the request message
  }

  // Smallest possible valid message is "/" + 3 mfr chars + 1 baud char +
  // CRLF = 7 bytes. Anything shorter, or not starting with "/", isn't an
  // identification message at all.
  if (identification.length() < 7 || identification[0] != '/') {
    Serial.println("[IEC62056-21] identification message malformed");
    return -2;
  }

  char baudId = identification[4];
  if (!baudRateFromId(baudId, negotiatedBaud)) {
    Serial.println("[IEC62056-21] unrecognised baud-rate ID in identification message");
    return -3;  // baud-rate ID isn't one we have a mapping for
  }

  // Step 3: acknowledge and select that baud rate. The ACK is fixed except
  // for the baud digit, which we echo straight back from what the meter
  // just told us -- we're confirming "yes, switch to the rate you offered,"
  // not picking one ourselves. '0' (3rd byte) is the fixed protocol-mode
  // control character for normal mode C.
  const uint8_t ack[] = {0x06, '0', (uint8_t)baudId, '0', '\r', '\n'};
  head->send(ack, sizeof(ack));

  // Only now do we retune the link. The meter is still listening at 300
  // baud until it has received this ACK, so switching any earlier would
  // mean sending the ACK itself at the wrong speed.
  head->setBaudRate(negotiatedBaud);

  return 0;
}

bool Iec6205621Reader::parseObisFloat(const String &block,
                                       const char *obisCode, float &out) {
  int idx = block.indexOf(obisCode);
  if (idx < 0) return false;

  // OBIS values look like "1-0:1.8.0(001234.567*kWh)" -- skip past the
  // opening bracket and read the number.
  int openParen = block.indexOf('(', idx);
  if (openParen < 0) return false;

  out = block.substring(openParen + 1).toFloat();
  return true;
}

int Iec6205621Reader::get_import(float *val) {
  exportValid = false;

  uint32_t negotiatedBaud = 0;
  int hs = handshake(negotiatedBaud);
  if (hs != 0) {
    Serial.printf("[IEC62056-21] handshake failed (code %d)\n", hs);
    return EXIT_FAILURE;
  }

  String dataBlock = readUntil("!\r\n", DATA_TIMEOUT_MS);
  if (dataBlock.length() == 0) {
    Serial.println("[IEC62056-21] no data block received after ACK");
    return EXIT_FAILURE;
  }

  float importKwh, exportKwh;
  bool haveImport = parseObisFloat(dataBlock, "1-0:1.8.0", importKwh);
  bool haveExport = parseObisFloat(dataBlock, "1-0:2.8.0", exportKwh);
  if (!haveImport || !haveExport) {
    Serial.println("[IEC62056-21] data block missing required OBIS code");
    return EXIT_FAILURE;
  }

  *val = importKwh;
  cachedExport = exportKwh;
  exportValid = true;
  return EXIT_SUCCESS;
}

int Iec6205621Reader::get_export(float *val) {
  if (!exportValid) {
    return EXIT_FAILURE;
  }
  *val = cachedExport;
  return EXIT_SUCCESS;
}

int Iec6205621Reader::get_voltage(float *val) {
  (void)val;
  // The OBIS codes this reader parses (1.8.0 import, 2.8.0 export) don't
  // include a voltage reading -- nothing to answer with.
  return EXIT_FAILURE;
}
