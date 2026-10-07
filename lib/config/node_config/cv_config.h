#ifndef CV_CONFIG_H
#define CV_CONFIG_H

#include <cstdint>
#include "networking_config.h"

// HTTP credentials and timeout used over the AP.
struct HttpBusConfig {
  uint32_t requestTimeoutMs;
  // Empty user disables HTTP basic auth.
  char httpUser[32];
  char httpPass[64];
};

struct CamHttpConfig {
  uint32_t pollIntervalMs;

  HttpBusConfig bus;

  // AI-on-the-edge-device endpoint and the flow/ROI to read from it.
  char host[64];
  char path[32];
  char flowName[32];
};

struct CvNodeConfig {
  uint8_t uid[16];
  // Where this node is installed. Set by upstream (NVS), not by hardware.
  uint8_t communityId;
  uint8_t unitId;
  SoftApConfig ap;
  EspNowConfig espNow;
  EspNowPeerConfig substation;
  CamHttpConfig cam;
};

#endif