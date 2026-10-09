/**
 * @file
 * Per node config loaders and the serial command that edits NVS.
 *
 * Each loader fills its node's config struct, reading every field from NVS if
 * it was ever set there and falling back to the firmware default otherwise. A
 * fresh board therefore runs on defaults, and a firmware update can still
 * improve them. The node's `*_loader.cpp` holds its keys and defaults, so that
 * is the file to check for what a given board is actually running. NVS_KEYS.md
 * lists every key.
 *
 * @todo Replace nvsConfigPollSerial() with the upstream config channel.
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include "rs485_config.h"
#include "ir_config.h"
#include "cv_config.h"
#include "substation_config.h"
#include "gateway_config.h"

/**
 * Loads the RS485 node's config.
 *
 * @return Config from NVS and defaults, with the UID read from eFuse and the
 *         register addresses looked up from the configured MeterModel.
 */
Rs485NodeConfig loadRs485NodeConfig();

/**
 * Loads the IR node's config.
 *
 * @return Config from NVS and defaults, with the UID read from eFuse.
 */
IrNodeConfig loadIrNodeConfig();

/**
 * Loads the CV node's config.
 *
 * @return Config from NVS and defaults, with the UID read from eFuse.
 */
CvNodeConfig loadCvNodeConfig();

/**
 * Loads the substation's config.
 *
 * @return Config from NVS and defaults.
 */
SubstationConfig loadSubstationConfig();

/**
 * Loads the gateway's config.
 *
 * @return Config from NVS and defaults.
 */
GatewayConfig loadGatewayConfig();

/**
 * Handles serial config commands, without blocking. Call at the top of loop().
 *
 * Buffers serial input and runs each complete line as a command:
 * - `set <key> <value>` stores a number, e.g. `set poll_ms 5000`.
 * - `set <key> "<text>"` stores a string, e.g. `set ap_ssid "Kaizen"`.
 * - `clear` wipes NVS back to firmware defaults.
 * - `reboot` restarts the board.
 *
 * Changes take effect after a reboot, because the loaders only run in setup().
 */
void nvsConfigPollSerial();

#endif
