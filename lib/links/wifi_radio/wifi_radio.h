#ifndef WIFI_RADIO_H
#define WIFI_RADIO_H

#include "networking_config.h"

/*
 * Owns the C3's one radio: mode, channel, tx power, softAP. Everything above
 * this (HttpBus, EspNowUplink) only uses the radio, it never configures it,
 * so the channel and mode can't drift out of sync between them.
 *
 * The two starts add to each other rather than replace each other, in either
 * order, on one shared channel. EspNowUplink starts the station itself, and
 * ESP-NOW always runs on it. The softAP is only for nodes that host a network
 * (CV camera, ModbusTCP) and sits beside the station.
 */

// Adds the softAP on the given channel.
bool wifiRadioStartAp(const SoftApConfig &cfg, uint8_t channel);
// Adds an unconnected station pinned to the channel. Safe to repeat.
bool wifiRadioStartStation(uint8_t channel);
// True once the softAP is up.
bool wifiRadioApUp();
// True while at least one station has joined the AP.
bool wifiRadioHasStations();

#endif
