#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include "rs485_config.h"
#include "ir_config.h"
#include "cv_config.h"
#include "substation_config.h"
#include "gateway_config.h"

// Loads a node's config from NVS, falling back to firmware defaults for any
// key that was never set. See the node's *_loader.cpp in lib/nvs_config/ for
// its keys and defaults; that is the file to check for what a given board is
// actually running.
Rs485NodeConfig loadRs485NodeConfig();
IrNodeConfig loadIrNodeConfig();
CvNodeConfig loadCvNodeConfig();
SubstationConfig loadSubstationConfig();
GatewayConfig loadGatewayConfig();

// Non-blocking; call at the top of loop(). Stands in for an upstream config
// channel: "set <key> <value>" ("set poll_ms 5000", or quote the value for
// text, e.g. set ap_ssid "Kaizen"), "clear" wipes NVS back to defaults,
// "reboot" restarts the board.
void nvsConfigPollSerial();

#endif
