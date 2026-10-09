/**
 * @file
 * Owner of the ESP32-C3's one radio: mode, channel, transmit power, softAP.
 *
 * Everything above this (HttpBus, EspNowUplink) only uses the radio, it never
 * configures it, so the channel and mode cannot drift out of sync between
 * them.
 *
 * The two starts add to each other rather than replace each other, in either
 * order, on one shared channel. Both refuse a channel different from the one
 * the radio is already on. EspNowUplink starts the station itself, and
 * ESP-NOW always runs on it. The softAP is only for nodes that host a network
 * (the CV camera, RS485 in ModbusTCP mode) and sits beside the station.
 */

#ifndef WIFI_RADIO_H
#define WIFI_RADIO_H

#include "networking_config.h"

/**
 * Adds the softAP on the given channel, at 192.168.4.1/24.
 *
 * @param[in] cfg      Network name and passphrase.
 * @param[in] channel  Wi-Fi channel. Must match any channel already in use.
 * @retval true   The AP is up.
 * @retval false  The radio is on another channel, or the AP failed to start.
 */
bool wifiRadioStartAp(const SoftApConfig &cfg, uint8_t channel);

/**
 * Adds an unconnected station interface pinned to the channel.
 *
 * Safe to call again, a repeat call on the same channel does nothing.
 *
 * @param[in] channel  Wi-Fi channel. Must match any channel already in use.
 * @retval true   The station is up.
 * @retval false  The radio is on another channel, or setting it failed.
 */
bool wifiRadioStartStation(uint8_t channel);

/**
 * Reports whether the softAP is up.
 *
 * @retval true   wifiRadioStartAp() succeeded.
 * @retval false  No AP has been started.
 */
bool wifiRadioApUp();

/**
 * Reports whether any device has joined the softAP.
 *
 * @retval true   At least one station is connected.
 * @retval false  Nothing has joined.
 */
bool wifiRadioHasStations();

#endif
