#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include "node_config.h"

// Loads a node's config from NVS, falling back to firmware defaults for any
// key that was never set. See src/nodes/nvs_config.cpp for the full key
// table and defaults; this is the single file to check for what any given
// board is actually running.
Rs485NodeConfig loadRs485NodeConfig();
CvNodeConfig loadCvNodeConfig();
SubstationConfig loadSubstationConfig();
GatewayConfig loadGatewayConfig();

// Non-blocking; call at the top of loop(). Stands in for an upstream config
// channel: "set <key> <value>" ("set poll_ms 5000", or quote the value for
// text, e.g. set ap_ssid "Kaizen"), "clear" wipes NVS back to defaults,
// "reboot" restarts the board.
void nvsConfigPollSerial();

#endif
