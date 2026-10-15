/**
 * @file
 * Per node config loaders and the serial command that edits NVS.
 *
 * Each loader fills its node's config struct, reading every field from NVS if
 * it was ever set there and falling back to the firmware default otherwise. A
 * fresh board therefore runs on defaults, and a firmware update can still
 * improve them. A loader fails only when NVS itself cannot be opened, and the
 * node then idles rather than run on defaults it never checked. The node's `*_loader.cpp` holds its keys and defaults, so that
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
 * @param[out] cfg  Filled with the config from NVS and defaults, with the UID
 *                  read from eFuse and the register addresses looked up from
 *                  the configured MeterModel.
 * @retval EXIT_SUCCESS  Loaded.
 * @retval EXIT_FAILURE  NVS could not be opened. @p cfg is unchanged.
 */
int loadRs485NodeConfig(Rs485NodeConfig &cfg);

/**
 * Loads the IR node's config.
 *
 * @param[out] cfg  Filled with the config from NVS and defaults, with the UID
 *                  read from eFuse.
 * @retval EXIT_SUCCESS  Loaded.
 * @retval EXIT_FAILURE  NVS could not be opened. @p cfg is unchanged.
 */
int loadIrNodeConfig(IrNodeConfig &cfg);

/**
 * Loads the CV node's config.
 *
 * @param[out] cfg  Filled with the config from NVS and defaults, with the UID
 *                  read from eFuse.
 * @retval EXIT_SUCCESS  Loaded.
 * @retval EXIT_FAILURE  NVS could not be opened. @p cfg is unchanged.
 */
int loadCvNodeConfig(CvNodeConfig &cfg);

/**
 * Loads the substation's config.
 *
 * @param[out] cfg  Filled with the config from NVS and defaults.
 * @retval EXIT_SUCCESS  Loaded.
 * @retval EXIT_FAILURE  NVS could not be opened. @p cfg is unchanged.
 */
int loadSubstationConfig(SubstationConfig &cfg);

/**
 * Loads the gateway's config.
 *
 * @param[out] cfg  Filled with the config from NVS and defaults.
 * @retval EXIT_SUCCESS  Loaded.
 * @retval EXIT_FAILURE  NVS could not be opened. @p cfg is unchanged.
 */
int loadGatewayConfig(GatewayConfig &cfg);

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
