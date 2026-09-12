#ifndef WIFI_RADIO_H
#define WIFI_RADIO_H

#include "node_config.h"

/*
 * Owns the C3's one radio: mode, channel, tx power, softAP. Everything above
 * this (HttpBus, EspNowUplink) only uses the radio, it never configures it,
 * so the channel and mode can't drift out of sync between them.
 */

// Brings up the softAP. Must be called once before HttpBus or EspNowUplink.
bool wifiRadioStart(const WifiRadioConfig &cfg);
// True once the softAP is up.
bool wifiRadioUp();
// True while at least one station has joined the AP.
bool wifiRadioHasStations();

#endif
