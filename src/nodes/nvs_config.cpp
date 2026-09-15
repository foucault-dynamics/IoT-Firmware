#include "nvs_config.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstdio>
#include <cstring>

#include "secrets.h"

namespace {

const char *NVS_NAMESPACE = "config";

// RS485 bus pins. Board wiring, never NVS keys.
const uint8_t RS485_RX_PIN = 8;
const uint8_t RS485_TX_PIN = 9;
const uint8_t RS485_DERE_PIN = 10;

// LoRa SPI pins. Same reasoning.
const LoRaPins LORA_PINS = {/*sck*/ 4, /*miso*/ 5, /*mosi*/ 6, /*ss*/ 7, /*rst*/ 3, /*dio0*/ 1};

struct MeterModelEntry {
  MeterModel model;
  uint16_t voltage;
  uint16_t import_energy;
  uint16_t export_energy;
};

const MeterModelEntry METER_MODELS[] = {
    {MeterModel::Simulated_Serial, /*voltage*/ 0, /*import*/ 4, /*export*/ 2},
    {MeterModel::Simulated_Tcp, 0, 4, 2},
};

const MeterModelEntry &lookupMeterModel(MeterModel model) {
  for (const auto &entry : METER_MODELS) {
    if (entry.model == model) {
      return entry;
    }
  }
  // Unknown model: fall back to the first row so the node still runs.
  return METER_MODELS[0];
}

Preferences prefs;

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

bool parseMac(const String &text, uint8_t out[6]) {
  unsigned int bytes[6];
  if (sscanf(text.c_str(), "%x:%x:%x:%x:%x:%x", &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) {
    out[i] = static_cast<uint8_t>(bytes[i]);
  }
  return true;
}

void readMac(const char *key, const uint8_t fallback[6], uint8_t out[6]) {
  if (prefs.isKey(key)) {
    String value = prefs.getString(key, "");
    if (parseMac(value, out)) {
      Serial.printf("[CFG] %s = %s (NVS)\n", key, value.c_str());
      return;
    }
    Serial.printf("[CFG] %s malformed in NVS, using default\n", key);
  }
  memcpy(out, fallback, 6);
}

LoRaConfig loadLoRaConfig() {
  LoRaConfig cfg{};
  cfg.band = readU32("lora_band", static_cast<uint32_t>(SECRET_LORA_BAND));
  cfg.spreadingFactor = static_cast<uint8_t>(readU32("lora_sf", 10));
  cfg.bandwidth = readU32("lora_bw", 125000);
  cfg.syncWord = static_cast<uint8_t>(readU32("lora_sync", 0xF3));
  cfg.txPower = static_cast<uint8_t>(readU32("lora_txpwr", 14));
  return cfg;
}

const size_t NVS_MAX_KEY_LEN = 15;

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

Rs485NodeConfig loadRs485NodeConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  Rs485NodeConfig cfg{};

  cfg.readerType = static_cast<ReaderType>(readU32("reader", static_cast<uint32_t>(ReaderType::ModbusTCP)));

  readStr("ap_ssid", SECRET_AP_SSID, cfg.radio.ssid, sizeof(cfg.radio.ssid));
  readStr("ap_pass", SECRET_AP_PASS, cfg.radio.password, sizeof(cfg.radio.password));
  cfg.radio.channel = static_cast<uint8_t>(readU32("ap_channel", 6));

  cfg.espNow.sendTimeoutMs = readU32("espnow_to_ms", 100);

  uint8_t defaultMac[] = SECRET_MAC;
  readMac("sub_mac", defaultMac, cfg.substation.mac);

  MeterModel model = static_cast<MeterModel>(readU32("meter_model", static_cast<uint32_t>(MeterModel::Simulated_Tcp)));
  const MeterModelEntry &entry = lookupMeterModel(model);

  cfg.modbus.meterModel = model;
  cfg.modbus.pollIntervalMs = readU32("poll_ms", 1000);
  cfg.modbus.bus.rx = RS485_RX_PIN;
  cfg.modbus.bus.tx = RS485_TX_PIN;
  cfg.modbus.bus.dere = RS485_DERE_PIN;
  cfg.modbus.bus.baudRate = readU32("baud", 9600);
  cfg.modbus.bus.format = SERIAL_8N1;
  cfg.modbus.registerFormat = RegisterFormat::IEEE_754Float;
  cfg.modbus.slaveAddress = static_cast<uint8_t>(readU32("slave_addr", 1));
  cfg.modbus.functionCode = 0x03;
  cfg.modbus.voltage_address = entry.voltage;
  cfg.modbus.import_address = entry.import_energy;
  cfg.modbus.export_address = entry.export_energy;

  readStr("tcp_host", SECRET_MODBUS_SIM_HOST, cfg.tcp.host, sizeof(cfg.tcp.host));
  cfg.tcp.port = static_cast<uint16_t>(readU32("tcp_port", SECRET_MODBUS_SIM_PORT));
  cfg.tcp.connectTimeoutMs = 3000;

  prefs.end();
  return cfg;
}

CvNodeConfig loadCvNodeConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  CvNodeConfig cfg{};

  cfg.uid = readU32("uid", 3);

  readStr("ap_ssid", SECRET_CAM_AP_SSID, cfg.radio.ssid, sizeof(cfg.radio.ssid));
  readStr("ap_pass", SECRET_CAM_AP_PASSWORD, cfg.radio.password, sizeof(cfg.radio.password));
  cfg.radio.channel = static_cast<uint8_t>(readU32("ap_channel", 1));

  cfg.espNow.sendTimeoutMs = readU32("espnow_to_ms", 200);

  uint8_t defaultMac[] = SECRET_MAC;
  readMac("sub_mac", defaultMac, cfg.substation.mac);

  cfg.cam.pollIntervalMs = readU32("poll_ms", 30000);
  cfg.cam.bus.requestTimeoutMs = 5000;
  readStr("cam_user", SECRET_CAM_USER, cfg.cam.bus.httpUser, sizeof(cfg.cam.bus.httpUser));
  readStr("cam_pass", SECRET_CAM_PASS, cfg.cam.bus.httpPass, sizeof(cfg.cam.bus.httpPass));
  readStr("cam_host", SECRET_CAM_HOST, cfg.cam.host, sizeof(cfg.cam.host));
  strncpy(cfg.cam.path, "/json", sizeof(cfg.cam.path) - 1);
  cfg.cam.path[sizeof(cfg.cam.path) - 1] = '\0';
  readStr("cam_flow", SECRET_CAM_FLOW_NAME, cfg.cam.flowName, sizeof(cfg.cam.flowName));

  prefs.end();
  return cfg;
}

SubstationConfig loadSubstationConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  SubstationConfig cfg{};
  cfg.lora = loadLoRaConfig();
  cfg.maxRetries = static_cast<uint8_t>(readU32("lora_retries", 3));
  cfg.ackTimeoutMs = readU32("lora_ack_ms", 1500);

  prefs.end();
  return cfg;
}

GatewayConfig loadGatewayConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  GatewayConfig cfg{};
  cfg.lora = loadLoRaConfig();

  readStr("wifi_ssid", SECRET_WIFI_SSID, cfg.wifi.ssid, sizeof(cfg.wifi.ssid));
  readStr("wifi_pass", SECRET_WIFI_PASS, cfg.wifi.password, sizeof(cfg.wifi.password));

  cfg.mqtt.enabled = readU32("mqtt_on", 1) != 0;
  readStr("mqtt_host", SECRET_MQTT_SERVER, cfg.mqtt.server, sizeof(cfg.mqtt.server));
  cfg.mqtt.port = static_cast<uint16_t>(readU32("mqtt_port", SECRET_MQTT_PORT));
  readStr("mqtt_topic", SECRET_MQTT_TOPIC, cfg.mqtt.topic, sizeof(cfg.mqtt.topic));

  prefs.end();
  return cfg;
}

LoRaPins loraPins() {
  return LORA_PINS;
}

void nvsConfigPollSerial() {
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
    if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}
