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
 * All methods return 0 or a byte count on success, negative on error.
 */
class Module {
 public:
  virtual ~Module() = default;

  // Configure GPIOs and bring the peripheral up. Call once from setup().
  virtual int setup() = 0;

  // Transmit len bytes. Returns bytes sent.
  virtual int send(const uint8_t *data, size_t len) = 0;

  // Read up to maxLen bytes into buf. Returns bytes read (0 if none).
  virtual int receive(uint8_t *buf, size_t maxLen) = 0;

  // True if at least one byte is waiting to be read.
  virtual bool available() = 0;
};

#endif
