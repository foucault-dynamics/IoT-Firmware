#ifndef IR_CONFIG_H
#define IR_CONFIG_H

#include <cstdint>
#include "HardwareSerial.h"
#include "networking_config.h"

struct IrConfig {
  uint8_t rx, tx;
  uint32_t baudRate;
  SerialConfig format;
};

struct Iec62056Config {
  uint32_t pollIntervalMs;

  IrConfig bus;
};

struct IrNodeConfig {
  uint8_t uid[16];
  // Where this node is installed. Set by upstream (NVS), not by hardware.
  uint8_t communityId;
  uint8_t unitId;
  // Picks RealIrHead vs SimulatedIrHead at runtime (the EE team's
  // UART-to-IR circuit doesn't exist yet).
  bool simulate;
  EspNowConfig espNow;
  EspNowPeerConfig substation;
  Iec62056Config iec;
};

#endif