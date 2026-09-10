#ifndef TRANSMITTER_H
#define TRANSMITTER_H

#include <cstddef>
#include <cstdint>

class Transmitter{
 public:
  virtual ~Transmitter() = default;
  virtual int init() = 0;
  virtual int sendPacket(const void *address, const uint8_t *buf, size_t len) = 0;
  virtual int receivePacket(void *address, uint8_t *buf, size_t bufLen) = 0;
};

#endif
