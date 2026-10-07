/**
 * @file
 * Reader, the base class for a meter protocol running on top of a Module.
 */

#ifndef READER_H
#define READER_H

#include "module.h"

/**
 * Base class for a meter protocol that turns raw bus bytes into readings.
 *
 * A Reader holds a Module and speaks one protocol over it, for example Modbus
 * RTU registers or IEC 62056-21 OBIS codes. Each concrete reader takes its own
 * config struct in its constructor, which keeps protocol specific fields out
 * of this shared interface.
 *
 * Every getter writes through a pointer and returns a status, so a node can
 * skip a value the protocol cannot provide.
 */
class Reader {
 protected:
  // Brace initialised on purpose: Doxygen 1.18 misreads `module = nullptr` as
  // a C++20 module declaration and fails the docs build.
  Module *module{nullptr};  ///< Bus the protocol runs over, set by init().

 public:
  virtual ~Reader() = default;

  /**
   * Attaches the reader to its bus and prepares the protocol.
   *
   * @param[in] module  Bus to read the meter over. Must outlive the reader.
   * @retval EXIT_SUCCESS  Ready to read.
   * @retval EXIT_FAILURE  Config or bus setup was invalid.
   */
  virtual int init(Module &module) = 0;

  /**
   * Reads the imported energy register (OBIS 1.8.0).
   *
   * @param[out] val  Imported energy in kWh. Untouched on failure.
   * @retval EXIT_SUCCESS  @p val holds a fresh reading.
   * @retval EXIT_FAILURE  The meter could not be read.
   */
  virtual int get_import(float *val) = 0;

  /**
   * Reads the exported energy register (OBIS 2.8.0).
   *
   * @param[out] val  Exported energy in kWh. Untouched on failure.
   * @retval EXIT_SUCCESS  @p val holds a fresh reading.
   * @retval EXIT_FAILURE  The meter could not be read, or the protocol has no
   *                       export value.
   */
  virtual int get_export(float *val) = 0;

  /**
   * Reads the grid voltage.
   *
   * @param[out] val  Voltage in volts. Untouched on failure.
   * @retval EXIT_SUCCESS  @p val holds a fresh reading.
   * @retval EXIT_FAILURE  The meter could not be read, or the protocol has no
   *                       voltage value.
   */
  virtual int get_voltage(float *val) = 0;
};

#endif
