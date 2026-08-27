#include "tcp_bus.h"
#include <Arduino.h>
#include <cstdlib>

TcpBus::TcpBus(TcpBusConfig config) {
  this->config = config;
}

void TcpBus::init() {
  if (ensureConnected()) {
    Serial.println("[TcpBus] connected to sim");
  } else {
    Serial.println("[TcpBus] initial connect failed, will retry on send()");
  }
}

bool TcpBus::ensureConnected() {
  if (client.connected()) {
    return true;
  }
  client.stop();
  if (!client.connect(config.host, config.port, config.connectTimeoutMs)) {
    Serial.println("[TcpBus] connect failed");
    return false;
  }
  client.setNoDelay(true);
  return true;
}


int TcpBus::send(const uint8_t *data, size_t len) {
  // Discard any stale RX bytes so a late response from a timed-out poll
  // doesn't desync the next frame.
  while (client.available()) {
    client.read();
  }

  if (!ensureConnected()) {
    return EXIT_FAILURE;
  }

  size_t sent = client.write(data, len);
  if (sent != len) {
    Serial.println("[TcpBus] error sending data through TCP socket");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int TcpBus::readByte() {
  return available() ? client.read() : -1;
}

bool TcpBus::available() {
  return client.available() > 0;
}
