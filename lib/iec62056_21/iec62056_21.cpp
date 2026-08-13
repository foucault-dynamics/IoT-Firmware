#include "iec62056_21.h"
#include <Arduino.h>

namespace {
constexpr unsigned long ID_TIMEOUT_MS = 2000;
constexpr unsigned long DATA_TIMEOUT_MS = 3000;
constexpr uint32_t DATA_BAUD = 19200;
}  // namespace

Iec6205621Reader::Iec6205621Reader(IrHead &head) : head(head) {}

int Iec6205621Reader::setup() { return head.setup(); }

String Iec6205621Reader::readUntil(const char *terminator,
                                    unsigned long timeoutMs) {
  String result;
  unsigned long deadline = millis() + timeoutMs;
  uint8_t byte;

  while (millis() < deadline) {
    if (head.available() && head.receive(&byte, 1) == 1) {
      result += (char)byte;
      if (result.endsWith(terminator)) break;
    }
  }
  return result;
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

int Iec6205621Reader::poll(Payload &out) {
  const uint8_t request[] = {'/', '?', '!', '\r', '\n'};
  head.send(request, sizeof(request));

  String identification = readUntil("\r\n", ID_TIMEOUT_MS);
  if (identification.length() == 0) {
    return -1;  // no meter responded to the initiation sequence
  }

  const uint8_t ack[] = {0x06, '0', '5', '0', '\r', '\n'};
  head.send(ack, sizeof(ack));
  head.setBaudRate(DATA_BAUD);

  String dataBlock = readUntil("!\r\n", DATA_TIMEOUT_MS);
  if (dataBlock.length() == 0) {
    return -2;  // no data block received after the ACK
  }

  float importKwh, exportKwh;
  bool haveImport = parseObisFloat(dataBlock, "1-0:1.8.0", importKwh);
  bool haveExport = parseObisFloat(dataBlock, "1-0:2.8.0", exportKwh);
  if (!haveImport || !haveExport) {
    return -3;  // data block didn't contain the OBIS codes we need
  }

  out.kwh_import = importKwh;
  out.kwh_export = exportKwh;
  return 0;
}
