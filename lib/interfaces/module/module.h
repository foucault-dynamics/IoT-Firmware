/**
 * @file
 * Module, the base class for anything that moves raw bytes over one bus.
 */

#ifndef MODULE_H
#define MODULE_H

#include <cstddef>
#include <cstdint>

/**
 * Base class for every communication module on the board.
 *
 * A Module is anything with a part number that moves raw bytes: the SP3485
 * transceiver, the SX1276 radio, the IR head, an HTTP or TCP client. It knows
 * its own pins and its own peripheral, and nothing about the meaning of the
 * bytes it carries. Protocols (Modbus RTU, IEC 62056-21, the ESP32-CAM API)
 * sit on top of a Module by holding a reference to it, see Reader.
 */
class Module {
 public:
  virtual ~Module() = default;

  /**
   * Brings the peripheral up so it is ready to send and receive.
   *
   * @retval EXIT_SUCCESS  Ready to use.
   * @retval EXIT_FAILURE  Setup failed, the module must not be used.
   */
  virtual int init() = 0;

  /**
   * Transmits a block of bytes.
   *
   * @param[in] data  Bytes to send.
   * @param[in] len   Number of bytes in @p data.
   * @retval EXIT_SUCCESS  All bytes were sent.
   * @retval EXIT_FAILURE  The send failed or was incomplete.
   */
  virtual int send(const uint8_t *data, size_t len) = 0;

  /**
   * Reads one received byte, without blocking.
   *
   * @return The byte (0 to 255), or -1 if nothing is waiting.
   */
  virtual int readByte() = 0;

  /**
   * Reports whether a received byte is waiting.
   *
   * @retval true   At least one byte can be read with readByte().
   * @retval false  Nothing is waiting.
   */
  virtual bool available() = 0;
};

#endif
