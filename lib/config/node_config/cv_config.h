/**
 * @file
 * Config structs for the CV (camera) node.
 */

#ifndef CV_CONFIG_H
#define CV_CONFIG_H

#include <cstdint>
#include "networking_config.h"

/** How the CV node reaches the cam, picked by the `cam_link` NVS key. */
enum class CamLink : uint8_t {
  Http = 0,  ///< HttpBus over the node's Wi-Fi AP.
  Uart = 1,  ///< UartCamBus over a wire to the cam's UART.
};

/** UART pins, rate and timeout UartCamBus uses to reach the cam over a wire. */
struct UartCamConfig {
  uint8_t rx;          ///< C3 GPIO wired to the cam's TX.
  uint8_t tx;          ///< C3 GPIO wired to the cam's RX.
  uint32_t baud;       ///< Line rate in baud, 8N1. Must match the cam.
  uint32_t timeoutMs;  ///< Time allowed for a whole reply, in ms.
};

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

  char host[64];            ///< Cam's IP address or hostname on the AP.
  char path[32];            ///< AI-on-the-edge-device API path, "/json".
  char flowName[32];        ///< Flow (number) name of the import reading, as set in the cam's config.ini.
  char exportFlowName[32];  ///< Flow (number) name of the export reading. Empty if the cam does not read export.
};

/** Everything the CV node needs, filled in by loadCvNodeConfig(). */
struct CvNodeConfig {
  uint8_t uid[16];               ///< This board's eFuse UID.
  uint8_t communityId;           ///< Where the node is installed. Set from NVS, not hardware.
  uint8_t unitId;                ///< Unit within the community. Set from NVS, not hardware.
  CamLink camLink;               ///< HTTP over the AP, or UART over a wire.
  SoftApConfig ap;               ///< AP the cam joins. CamLink::Http only.
  UartCamConfig camUart;         ///< Wire to the cam. CamLink::Uart only.
  EspNowConfig espNow;           ///< Uplink to the substation.
  CamHttpConfig cam;             ///< Cam reader settings, used by both links.
};

#endif
