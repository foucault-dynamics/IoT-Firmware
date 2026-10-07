/**
 * @file
 * Config structs for the CV (camera) node.
 */

#ifndef CV_CONFIG_H
#define CV_CONFIG_H

#include <cstdint>
#include "networking_config.h"

/** HTTP credentials and timeout HttpBus uses to reach the cam over the AP. */
struct HttpBusConfig {
  uint32_t requestTimeoutMs;  ///< Timeout for one GET, in ms.
  char httpUser[32];          ///< Basic auth user. Empty disables auth.
  char httpPass[64];          ///< Basic auth password.
};

/** Where and how often CamHttpReader asks the cam for a reading. */
struct CamHttpConfig {
  uint32_t pollIntervalMs;  ///< Time between readings, in ms.

  HttpBusConfig bus;  ///< HTTP client settings.

  char host[64];      ///< Cam's IP address or hostname on the AP.
  char path[32];      ///< AI-on-the-edge-device API path, "/json".
  char flowName[32];  ///< Flow (number) name to read, as set in the cam's config.ini.
};

/** Everything the CV node needs, filled in by loadCvNodeConfig(). */
struct CvNodeConfig {
  uint8_t uid[16];               ///< This board's eFuse UID.
  uint8_t communityId;           ///< Where the node is installed. Set from NVS, not hardware.
  uint8_t unitId;                ///< Unit within the community. Set from NVS, not hardware.
  SoftApConfig ap;               ///< AP the cam joins.
  EspNowConfig espNow;           ///< Uplink to the substation.
  EspNowPeerConfig substation;   ///< Substation to send readings to.
  CamHttpConfig cam;             ///< Cam reader settings.
};

#endif
