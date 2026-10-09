/**
 * @file
 * TcpBus, a Module carrying Modbus RTU frames over TCP for simulator testing.
 */

#ifndef TCP_BUS_H
#define TCP_BUS_H

#include "module.h"
#include "rs485_config.h"
#include <WiFi.h>
#include <cstdint>

/**
 * TCP transport carrying raw Modbus RTU bytes (RTU over TCP), as spoken by
 * ModbusSim/ModbusSimTCP.py.
 *
 * Same wire format as Sp3485, over a WiFiClient socket instead of RS485. This
 * lets the RS485 node be developed with no transceiver and no meter on the
 * desk.
 */
class TcpBus : public Module {
 private:
  TcpBusConfig config;  ///< Simulator address and connect timeout.
  WiFiClient client;    ///< Socket to the simulator.

  /**
   * Makes sure the socket is connected, reconnecting if it dropped.
   *
   * @retval true   The socket is usable.
   * @retval false  The reconnect failed.
   */
  bool ensureConnected();

 public:
  /**
   * Stores the config. Nothing connects until init().
   *
   * @param[in] config  Simulator address and connect timeout.
   */
  TcpBus(TcpBusConfig config);

  /**
   * Opens the first connection to the simulator.
   *
   * @retval EXIT_SUCCESS  Connected.
   * @retval EXIT_FAILURE  Could not connect. send() retries on its own.
   */
  int init() override;

  int readByte() override;

  /**
   * Sends one frame, reconnecting first if the socket dropped.
   *
   * Stale RX bytes are discarded first so a late reply to a timed out poll is
   * not read as the start of the next frame.
   *
   * @param[in] data  Frame bytes.
   * @param[in] len   Number of bytes in @p data.
   * @retval EXIT_SUCCESS  Every byte was written.
   * @retval EXIT_FAILURE  Not connected, or the write was short.
   */
  int send(const uint8_t *data, size_t len) override;

  bool available() override;
};

#endif
