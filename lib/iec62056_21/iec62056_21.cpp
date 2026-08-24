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

  // This part sends the universal "wake up" message used in all 
  // IEC62056 interactions
  const uint8_t request[] = {'/', '?', '!', '\r', '\n'};
  head.send(request, sizeof(request));

  // read back the meter's ID, timeout if taking too long
  String identification = readUntil("\r\n", ID_TIMEOUT_MS);
  // Return false if there's no meter recieved
  if (identification.length() == 0) {
    return -1;  // no meter responded to the initiation sequence
  }

  // Send back an acknowledgement to the meter of its response, 
  // universal "050" message. Set the baud rate to 19200. It's at 300 before
  // at the handshake sped. 
  // The speed is 300 bits per second because it's slow and universal and 
  // all meters can manage that reliably. If the opening baud rate was 19200
  // old meters wouldn't even be able to respond
  const uint8_t ack[] = {0x06, '0', '5', '0', '\r', '\n'};
  head.send(ack, sizeof(ack));
  head.setBaudRate(DATA_BAUD);

  // read the data block sent by the meter until the "!"
  String dataBlock = readUntil("!\r\n", DATA_TIMEOUT_MS);
  if (dataBlock.length() == 0) {
    return -2;  // no data block received after the ACK
  }

  // pulls the two numbers out for export nad import values
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
