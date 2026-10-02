#ifndef IEC62056_21_H
#define IEC62056_21_H

#include "ir_head.h"
#include "module.h"
#include "node_config.h"
#include "reader.h"
#include <WString.h>

/*
 * IEC 62056-21 mode C protocol reader, run over an IrHead (real or
 * simulated). Speaks the standard optical-port handshake and pulls the
 * import/export energy readings out of the data block by OBIS code.
 */
class Iec6205621Reader : public Reader {
 private:
  IrHead *head = nullptr;
  // Kept for interface symmetry with ModbusRtuReader/CamHttpReader;
  // nothing in this protocol is actually per-meter configurable.
  Iec62056Config config;

  // Set by get_import(), consumed by the next get_export() call this cycle.
  float cachedExport = 0.0f;
  bool exportValid = false;

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

  // Runs the wake-up handshake: sends the request message, reads the
  // meter's identification response, and negotiates + switches to the
  // baud rate the meter's identification message asked for.
  //
  // On success, returns 0 and fills negotiatedBaud; `head` has already been
  // switched to that baud rate. On failure, negotiatedBaud is untouched and
  // the return value says why:
  //   -1  meter never responded to the request message
  //   -2  response wasn't shaped like a valid identification message
  //   -3  identification message used a baud-rate ID we don't recognize
  int handshake(uint32_t &negotiatedBaud);

 public:
  explicit Iec6205621Reader(const Iec62056Config &config);

  int init(Module &module) override;

  // Runs the handshake, reads the data block, and parses both OBIS codes.
  // On success returns EXIT_SUCCESS with *val set to the import reading,
  // and caches the export reading for the next get_export() call.
  int get_import(float *val) override;
  // Returns the export reading cached by the most recent successful
  // get_import() call this cycle. EXIT_FAILURE if get_import() hasn't
  // succeeded yet -- callers must call get_import() first.
  int get_export(float *val) override;
  // The OBIS codes this reader parses (1.8.0 import, 2.8.0 export) don't
  // include a voltage reading -- nothing to answer with.
  int get_voltage(float *val) override;
};

#endif
