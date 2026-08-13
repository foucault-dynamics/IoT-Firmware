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

 public:
  explicit Iec6205621Reader(IrHead &head);

  int setup();

  // Runs one full handshake + read cycle. Fills out.kwh_import and
  // out.kwh_export. Returns 0 on success, negative on failure (timeout,
  // missing OBIS code, etc).
  int poll(Payload &out);
};

#endif
