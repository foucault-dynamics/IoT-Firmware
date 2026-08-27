#ifndef IEC62056_21_H
#define IEC62056_21_H

#include "ir_head.h"
#include "shared_payload.h"
#include <WString.h>

/*
 * IEC 62056-21 mode C protocol reader, run over an IrHead (real or
 * simulated). Speaks the standard optical-port handshake and pulls the
 * import/export energy readings out of the data block by OBIS code.
 */
class Iec6205621Reader {
 private:
  IrHead &head;

  // Reads bytes from `head` until `terminator` is seen or `timeoutMs`
  // elapses. Returns everything read, including the terminator.
  String readUntil(const char *terminator, unsigned long timeoutMs);

  static bool parseObisFloat(const String &block, const char *obisCode,
                              float &out);

  // Maps a single IEC 62056-21 baud-rate ID character -- the 5th byte of
  // the identification message -- to bits per second, per the standard's
  // Table 6 ('0'=300 ... '6'=19200). Returns false if `code` isn't one of
  // the defined IDs.
  static bool baudRateFromId(char code, uint32_t &baudOut);

 public:
  explicit Iec6205621Reader(IrHead &head);

  int setup();

  // Runs just the wake-up handshake: sends the request message, reads the
  // meter's identification response, and negotiates + switches to the
  // baud rate the meter's identification message asked for. Does NOT read
  // the data block -- that (and OBIS extraction) is poll()'s job, and this
  // function doesn't call poll() or vice versa yet.
  //
  // On success, returns 0 and fills negotiatedBaud; `head` has already been
  // switched to that baud rate. On failure, negotiatedBaud is untouched and
  // the return value says why:
  //   -1  meter never responded to the request message
  //   -2  response wasn't shaped like a valid identification message
  //   -3  identification message used a baud-rate ID we don't recognize
  int handshake(uint32_t &negotiatedBaud);

  // Runs one full handshake + read cycle. Fills out.kwh_import and
  // out.kwh_export. Returns 0 on success, negative on failure (timeout,
  // missing OBIS code, etc).
  int poll(Payload &out);
};

#endif
