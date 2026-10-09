/**
 * @file
 * Iec6205621Reader, the Reader for the IEC 62056-21 optical port protocol.
 */

#ifndef IEC62056_21_H
#define IEC62056_21_H

#include "ir_head.h"
#include "module.h"
#include "ir_config.h"
#include "reader.h"
#include <WString.h>

/**
 * IEC 62056-21 mode C reader, run over an IrHead (real or simulated).
 *
 * Speaks the standard optical port handshake, then pulls the import and
 * export energy readings out of the data block by OBIS code. One handshake
 * yields both readings, so get_import() reads the whole block and caches the
 * export value for get_export().
 */
class Iec6205621Reader : public Reader {
 private:
  IrHead *head = nullptr;  ///< The bus from init(), as an IrHead so the baud rate can change.
  /**
   * Reader settings.
   *
   * Only bus.baudRate is used, as the rate every handshake starts at.
   */
  Iec62056Config config;

  float cachedExport = 0.0f;  ///< Export reading from the last get_import().
  bool exportValid = false;   ///< True once get_import() has filled #cachedExport this cycle.

  /**
   * Reads from #head until a terminator arrives or time runs out.
   *
   * @param[in] terminator  String that ends the read, e.g. CRLF.
   * @param[in] timeoutMs   Longest time to keep reading, in ms.
   * @return Everything read, including the terminator. Empty if nothing came.
   */
  String readUntil(const char *terminator, unsigned long timeoutMs);

  /**
   * Reads an exact number of bytes from #head, or fewer if time runs out.
   *
   * @param[out] out        Buffer for at least @p count bytes.
   * @param[in]  count      Bytes wanted.
   * @param[in]  timeoutMs  Longest time to keep reading, in ms.
   * @return How many bytes were read.
   */
  size_t readBytes(uint8_t *out, size_t count, unsigned long timeoutMs);

  /**
   * Reads and checks the ETX and BCC that close a framed data block.
   *
   * readUntil() stops at "!\r\n", which leaves ETX and BCC waiting. Reading
   * them here stops them turning up at the start of the next identification
   * message. Unframed blocks (no STX, as SimulatedIrHead sends) pass with
   * nothing to check.
   *
   * @param[in] block  Data block as returned by readUntil().
   * @retval true   Unframed, or framed with a matching BCC.
   * @retval false  ETX missing or the BCC does not match.
   */
  bool checkFrame(const String &block);

  /**
   * Finds an OBIS code in a data block and parses the number after it.
   *
   * OBIS lines look like "1-0:1.8.0(001234.567*kWh)".
   *
   * @param[in]  block     Data block from the meter.
   * @param[in]  obisCode  Code to look for, e.g. "1-0:1.8.0".
   * @param[out] out       Parsed value. Untouched if the code is missing.
   * @retval true   Found and parsed.
   * @retval false  The code or its opening bracket was not in the block.
   */
  static bool parseObisFloat(const String &block, const char *obisCode,
                              float &out);

  /**
   * Maps an IEC 62056-21 baud rate ID to bits per second.
   *
   * The ID is the 5th byte of the identification message. The mapping is the
   * standard's Table 6, '0' is 300 through to '6' is 19200.
   *
   * @param[in]  code     Baud rate ID character.
   * @param[out] baudOut  Rate in baud. Untouched if @p code is unknown.
   * @retval true   @p code is one of the defined IDs.
   * @retval false  Unknown ID.
   */
  static bool baudRateFromId(char code, uint32_t &baudOut);

  /**
   * Runs the wake up handshake and switches to the meter's offered baud rate.
   *
   * Drops #head back to the starting rate and clears anything left over, sends
   * the request message, reads the meter's identification message (skipping
   * an echo of the request), ACKs the baud rate it offered, then retunes #head
   * to that rate.
   *
   * @param[out] negotiatedBaud  Rate both sides switched to. Untouched on
   *                             failure.
   * @retval 0   Success, #head is already at the new rate.
   * @retval -1  The meter never answered the request message.
   * @retval -2  The reply was not shaped like an identification message.
   * @retval -3  The identification message used an unknown baud rate ID.
   */
  int handshake(uint32_t &negotiatedBaud);

 public:
  /**
   * Stores the config. Nothing is sent until get_import().
   *
   * @param[in] config  Reader settings, copied.
   */
  explicit Iec6205621Reader(const Iec62056Config &config);

  /**
   * Attaches the IR head and initialises it.
   *
   * @param[in] module  Must be an IrHead (RealIrHead, SimulatedIrHead or
   *                    TcpIrHead),
   *                    because the protocol changes the baud rate mid session.
   * @retval EXIT_SUCCESS  Always.
   */
  int init(Module &module) override;

  /**
   * Runs a full session and returns the import reading.
   *
   * Runs the handshake, reads the data block and parses both OBIS codes. The
   * export reading is cached for the next get_export().
   *
   * @param[out] val  Imported energy in kWh. Untouched on failure.
   * @retval EXIT_SUCCESS  @p val holds a fresh reading.
   * @retval EXIT_FAILURE  Handshake failed, no data block, a bad ETX or BCC,
   *                       or an OBIS code was missing.
   */
  int get_import(float *val) override;

  /**
   * Returns the export reading cached by the last successful get_import().
   *
   * @param[out] val  Exported energy in kWh. Untouched on failure.
   * @retval EXIT_SUCCESS  @p val holds the cached reading.
   * @retval EXIT_FAILURE  get_import() has not succeeded this cycle. Callers
   *                       must call get_import() first.
   */
  int get_export(float *val) override;

  /**
   * Always fails, because the OBIS codes parsed here (1.8.0 import, 2.8.0
   * export) include no voltage reading.
   *
   * @param[out] val  Untouched.
   * @retval EXIT_FAILURE  Always.
   */
  int get_voltage(float *val) override;
};

#endif
