#ifndef MODULE_H
#define MODULE_H

#include <cstddef>
#include <cstdint>

/*
 * Base class for every communication module on the board: anything with
 * a part number that moves raw bytes (SP3485 transceiver, SX1276 radio,
 * IR head).
 *
 * A Module knows its own pins, its own peripheral, and nothing about the
 * meaning of the bytes it carries. Protocols (Modbus RTU, the ESP32-CAM
 * link) sit ON TOP of a Module by holding a Module& and calling into it.
 *
 * send() returns EXIT_SUCCESS or EXIT_FAILURE. readByte() returns the byte
 * it read, or -1 when nothing was waiting.
 */
class Module {  
 public:
  virtual ~Module() = default;

  // Setup of module
  virtual void init() = 0;

  // Transmit len bytes. Returns EXIT_SUCCESS or EXIT_FAILURE.
  virtual int send(const uint8_t *data, size_t len) = 0;

  // Read one byte. Returns the byte, or -1 if none is waiting.
  virtual int readByte() = 0;

  // True if at least one byte is waiting to be read.
  virtual bool available() = 0;
};

#endif
