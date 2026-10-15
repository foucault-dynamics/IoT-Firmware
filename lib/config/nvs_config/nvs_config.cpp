/**
 * @file
 * NVS read helpers and the serial config command handler.
 */

#include "nvs_config.h"
#include "nvs_read.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

const char *NVS_NAMESPACE = "config";

Preferences prefs;

namespace {

/** Longest key NVS accepts. */
const size_t NVS_MAX_KEY_LEN = 15;

/**
 * Runs one serial command line: `reboot`, `clear` or `set <key> <value>`.
 *
 * @param[in,out] line  Null terminated command. Modified in place by strtok().
 */
void processLine(char *line) {
  char *cmd = strtok(line, " ");
  if (cmd == nullptr) {
    return;
  }

  if (strcmp(cmd, "reboot") == 0) {
    ESP.restart();
    return;
  }

  if (strcmp(cmd, "clear") == 0) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    Serial.println("[CFG] cleared, reboot to apply");
    return;
  }

  if (strcmp(cmd, "set") == 0) {
    char *key = strtok(nullptr, " ");
    char *rest = strtok(nullptr, "");
    if (key == nullptr || rest == nullptr) {
      Serial.println("[CFG] usage: set <key> <value>");
      return;
    }
    if (strlen(key) > NVS_MAX_KEY_LEN) {
      Serial.println("[CFG] key too long (max 15 chars)");
      return;
    }

    prefs.begin(NVS_NAMESPACE, false);
    // A quoted value is stored as a string, anything else as a number.
    if (rest[0] == '"') {
      char *end = strrchr(rest, '"');
      if (end == nullptr || end == rest) {
        Serial.println("[CFG] malformed quoted value");
        prefs.end();
        return;
      }
      *end = '\0';
      prefs.putString(key, rest + 1);
    } else {
      prefs.putUInt(key, strtoul(rest, nullptr, 10));
    }
    prefs.end();
    Serial.println("[CFG] saved, reboot to apply");
    return;
  }

  Serial.println("[CFG] unknown command");
}

}  // namespace

int openConfigNvs() {
  return prefs.begin(NVS_NAMESPACE, false) ? EXIT_SUCCESS : EXIT_FAILURE;
}

uint32_t readU32(const char *key, uint32_t fallback) {
  if (prefs.isKey(key)) {
    uint32_t value = prefs.getUInt(key, fallback);
    Serial.printf("[CFG] %s = %lu (NVS)\n", key, static_cast<unsigned long>(value));
    return value;
  }
  return fallback;
}

void readStr(const char *key, const char *fallback, char *out, size_t outLen) {
  if (prefs.isKey(key)) {
    String value = prefs.getString(key, fallback);
    strncpy(out, value.c_str(), outLen - 1);
    out[outLen - 1] = '\0';
    Serial.printf("[CFG] %s = %s (NVS)\n", key, out);
    return;
  }
  strncpy(out, fallback, outLen - 1);
  out[outLen - 1] = '\0';
}

void nvsConfigPollSerial() {
  // Static so a line typed across several loop() calls is assembled in one place.
  static char line[64];
  static size_t len = 0;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        line[len] = '\0';
        processLine(line);
        len = 0;
      }
      continue;
    }
    // Characters past the buffer are dropped rather than overflowing it.
    if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}
