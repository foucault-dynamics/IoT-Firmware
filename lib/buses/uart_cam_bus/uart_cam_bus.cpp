/**
 * @file
 * UartCamBus implementation.
 */

#include "uart_cam_bus.h"

#include <cstdlib>
#include <cstring>

size_t camUartRequestLine(const uint8_t *url, size_t len, char *out, size_t outLen) {
  const char *s = reinterpret_cast<const char *>(url);
  size_t i = 0;

  // Skip "scheme://host", keeping only the path
  for (size_t k = 0; k + 2 < len; k++) {
    if (s[k] == ':' && s[k + 1] == '/' && s[k + 2] == '/') {
      i = k + 3;
      while (i < len && s[i] != '/') {
        i++;
      }
      break;
    }
  }

  const char *path = (i < len) ? s + i : "/";
  size_t pathLen = (i < len) ? len - i : 1;

  // "GET " + path + "\n" + terminator
  if (outLen < 4 + pathLen + 2) {
    return 0;
  }
  memcpy(out, "GET ", 4);
  memcpy(out + 4, path, pathLen);
  out[4 + pathLen] = '\n';
  out[5 + pathLen] = '\0';
  return 5 + pathLen;
}

void CamUartFrame::reset() {
  len = 0;
  inFrame = false;
  buf[0] = '\0';
}

CamUartFrame::Status CamUartFrame::feed(uint8_t b) {
  if (b == CAM_UART_STX) {
    len = 0;
    inFrame = true;
    return Pending;
  }
  if (!inFrame) {
    return Pending;
  }
  if (b == CAM_UART_ETX) {
    buf[len] = '\0';
    inFrame = false;
    return Done;
  }
  if (len >= CAM_UART_MAX_BODY) {
    len = 0;
    inFrame = false;
    return Overflow;
  }
  buf[len++] = static_cast<char>(b);
  return Pending;
}

UartCamBus::UartCamBus(const UartCamConfig &config, HardwareSerial &serial)
    : config(config), serial(&serial) {}

int UartCamBus::init() {
  if (config.baud == 0 || config.rx == config.tx) {
    Serial.println("[UartCamBus] Bad config: baud is 0 or RX and TX are the same pin.");
    return EXIT_FAILURE;
  }
  serial->begin(config.baud, SERIAL_8N1, config.rx, config.tx);
  Serial.printf("[UartCamBus] UART up, RX GPIO %u, TX GPIO %u, %lu baud\n", config.rx, config.tx,
                static_cast<unsigned long>(config.baud));
  return EXIT_SUCCESS;
}

int UartCamBus::send(const uint8_t *data, size_t len) {
  response = String();
  readIndex = 0;

  char line[96];
  size_t lineLen = camUartRequestLine(data, len, line, sizeof(line));
  if (lineLen == 0) {
    Serial.println("[UartCamBus] Request path too long.");
    return EXIT_FAILURE;
  }

  // Cam log lines and late replies to an earlier request
  while (serial->available()) {
    serial->read();
  }
  frame.reset();

  serial->write(reinterpret_cast<const uint8_t *>(line), lineLen);
  serial->flush();

  bool overflowed = false;
  unsigned long start = millis();
  while (millis() - start < config.timeoutMs) {
    while (serial->available()) {
      CamUartFrame::Status status = frame.feed(static_cast<uint8_t>(serial->read()));
      if (status == CamUartFrame::Done) {
        response = String(frame.body());
        return EXIT_SUCCESS;
      }
      if (status == CamUartFrame::Overflow) {
        overflowed = true;
      }
    }
    delay(1);
  }

  if (overflowed) {
    Serial.printf("[UartCamBus] Reply longer than %u bytes, dropped.\n", CAM_UART_MAX_BODY);
  } else {
    Serial.printf("[UartCamBus] No reply frame within %lu ms. Check wiring, baud and the cam side.\n",
                  static_cast<unsigned long>(config.timeoutMs));
  }
  return EXIT_FAILURE;
}

int UartCamBus::readByte() {
  if (!available()) {
    return -1;
  }
  return static_cast<uint8_t>(response[readIndex++]);
}

bool UartCamBus::available() {
  return readIndex < response.length();
}
