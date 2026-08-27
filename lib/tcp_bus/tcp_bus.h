#ifndef TCP_BUS_H
#define TCP_BUS_H

#include "module.h"
#include "node_config.h"
#include <WiFi.h>
#include <cstdint>

/*
 * TCP transport carrying raw Modbus RTU bytes (RTU-over-TCP), as spoken by
 * ModbusSim/ModbusSimTCP.py. Same wire format as Sp3485, over a WiFiClient
 * socket instead of RS485.
 */
class TcpBus : public Module {
 private:
  TcpBusConfig config;
  WiFiClient client;

  // Ensures a live connection, reconnecting if needed. Returns true if
  // the socket is usable afterwards.
  bool ensureConnected();

 public:
  TcpBus(TcpBusConfig config);
  void init() override;
  int readByte() override;
  int send(const uint8_t *data, size_t len) override;
  bool available() override;
};

#endif
