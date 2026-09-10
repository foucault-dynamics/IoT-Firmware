#ifndef SUBSTATION_H
#define SUBSTATION_H

#include "transmitter.h"
#include <cstdint>
#include <cstdlib>

class Substation :: public Transmitter{

public:
  int init() override;
  int sendPacket(const uint8_t *buf, size_t len);
  int receivePacket(uint8_t *buf);  
}

#endif
