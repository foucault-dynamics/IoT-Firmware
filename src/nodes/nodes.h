/**
 * @file
 * Setup and loop entry points for every node role, called from main.cpp.
 *
 * Each pair lives in its own file in src/nodes/, which assembles that node from
 * the libraries in lib/. A setup function that fails logs why and leaves the
 * node idle, and its loop function then does nothing. The end nodes share one
 * bring up, endNodeBringUp(), and a MeterGate that holds each loop off reading
 * until the meter answers a probe, printing "[TAG] Ready" when it does.
 */

#ifndef NODES_H
#define NODES_H


/** Loads config, builds the Modbus or DLMS/COSEM bus and reader, and runs the shared bring up. */
void rs485NodeSetup();
/** Waits for the meter to answer, then steps the read, send, sleep cycle. Never blocks between readings. */
void rs485NodeLoop();

/** Loads config, builds the IR head and reader, and runs the shared bring up. */
void irNodeSetup();
/** Waits for the meter to answer, then reads it and sends the result once per poll interval. */
void irNodeLoop();

/** Loads config, builds the HTTP bus and cam reader, and runs the shared bring up, AP included. */
void cvNodeSetup();
/** Waits for the cam to answer, then reads it and sends the result once per poll interval. */
void cvNodeLoop();

/** Loads config and starts LoRa and ESP-NOW. */
void substationSetup();
/** Buffers incoming ESP-NOW readings and relays the next one over LoRa. */
void substationLoop();

/** Loads config, starts LoRa and its receive task, and joins Wi-Fi and sets up MQTT if enabled. */
void gatewaySetup();
/** Keeps MQTT connected, then publishes the oldest queued LoRa reading. */
void gatewayLoop();

#endif
