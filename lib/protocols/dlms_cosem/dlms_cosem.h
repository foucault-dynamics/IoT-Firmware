/**
 * @file
 * DlmsCosemReader, the Reader for DLMS/COSEM meters over HDLC.
 */

#ifndef DLMS_COSEM_H
#define DLMS_COSEM_H

#include "hdlc.h"
#include "module.h"
#include "reader.h"
#include "rs485_config.h"

/** How long to wait for a complete frame, in ms. */
#define DLMS_TIMEOUT 1000

/**
 * DLMS/COSEM reader for meters on RS485, framed with HDLC.
 *
 * Connects as a client with no authentication or ciphering, and reads Register
 * objects by their OBIS code.
 */
class DlmsCosemReader : public Reader {
 public:
  /**
   * Stores the config. Nothing is sent until a getter is called.
   *
   * @param[in] config  Addresses, OBIS codes and bus settings, copied.
   */
  DlmsCosemReader(const DlmsCosemConfig &config);

  /**
   * Attaches the bus and encodes the client and server HDLC addresses.
   *
   * @param[in] module  Bus to poll the meter over.
   * @retval EXIT_SUCCESS  Ready to read.
   * @retval EXIT_FAILURE  An address did not fit its size, or the server
   *                       address size was not 1, 2 or 4.
   */
  int init(Module &module) override;

  int get_import(float *val) override;
  int get_export(float *val) override;
  int get_voltage(float *val) override;

 private:
#ifdef PIO_UNIT_TESTING
  friend class DlmsCosemReaderTest;
#endif

  DlmsCosemConfig config;  ///< Addresses, OBIS codes and bus settings.
  HdlcAddress client{};    ///< Encoded client SAP, set by init().
  HdlcAddress server{};    ///< Encoded server address, set by init().

  /**
   * Reads one HDLC frame, skipping noise and idle flags before it.
   *
   * Hunts for a flag followed by a type A format field, then reads as many
   * bytes as its length field gives. A length that cannot be a frame restarts
   * the hunt. The contents are left to hdlcParseFrame() to check.
   *
   * @param[out] buf  At least HDLC_FRAME_MAX bytes. Holds the frame, flags
   *                  included.
   * @param[out] len  Frame length.
   * @retval EXIT_SUCCESS  @p buf holds as many bytes as the length field gave.
   * @retval EXIT_FAILURE  No complete frame within DLMS_TIMEOUT.
   */
  int readFrame(uint8_t *buf, size_t *len);
};

#endif
