/**
 * @file
 * Setup and loop entry points for every node role, called from main.cpp.
 *
 * Each pair lives in its own file in src/nodes/, which assembles that node from
 * the libraries in lib/. A setup function that fails logs why and leaves the
 * node idle, and its loop function then does nothing.
 */

#ifndef NODES_H
#define NODES_H


/** Loads config, starts the ESP-NOW uplink and builds the Modbus or DLMS/COSEM bus and reader. */
void rs485NodeSetup();
/** Steps the read, send, sleep cycle. Never blocks between readings. */
void rs485NodeLoop();

/** Loads config, starts the ESP-NOW uplink and builds the IR head and reader. */
void irNodeSetup();
/** Reads the meter and sends the result once per poll interval. */
void irNodeLoop();

/** Loads config, starts the AP for the cam and the ESP-NOW uplink, and builds the reader. */
void cvNodeSetup();
/** Reads the cam and sends the result once per poll interval. */
void cvNodeLoop();

/** Loads config and starts LoRa and ESP-NOW. */
void substationSetup();
/** Buffers incoming ESP-NOW readings and relays the next one over LoRa. */
void substationLoop();

/** Loads config, starts LoRa, and joins Wi-Fi and sets up MQTT if enabled. */
void gatewaySetup();
/** Keeps MQTT connected, then receives one LoRa reading and publishes it. */
void gatewayLoop();

#endif
