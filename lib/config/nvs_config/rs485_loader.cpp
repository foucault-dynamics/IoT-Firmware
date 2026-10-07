#include "nvs_config.h"
#include "nvs_read.h"

#include "secrets.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

namespace {

// RS485 bus pins. Board wiring, never NVS
const uint8_t RS485_RX_PIN = 8;
const uint8_t RS485_TX_PIN = 9;
const uint8_t RS485_DERE_PIN = 10;

struct MeterModelEntry {
  MeterModel model;
  uint16_t voltage;
  uint16_t import_energy;
  uint16_t export_energy;
};

// Known meter models (better approach necessary in future)
const MeterModelEntry METER_MODELS[] = {
    {MeterModel::Simulated_Serial, /*voltage*/ 0, /*import*/ 4, /*export*/ 2},
    {MeterModel::Simulated_Tcp, 0, 4, 2},
};

// Get the model of a given meter (for registers and other meter specific settings)
const MeterModelEntry &lookupMeterModel(MeterModel model) {
  for (const auto &entry : METER_MODELS) {
    if (entry.model == model) {
      return entry;
    }
  }
  return METER_MODELS[0];
}

}  // namespace

// Load config for an RS485/Modbus meter node
Rs485NodeConfig loadRs485NodeConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  Rs485NodeConfig cfg{};

  // Set unique ID for board
#if CONFIG_IDF_TARGET_ESP32C3
  esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID,cfg.uid,sizeof(cfg.uid) * 8);
#endif

  cfg.communityId = static_cast<uint8_t>(readU32("comm_id", 0));
  cfg.unitId = static_cast<uint8_t>(readU32("unit_id", 0));

  cfg.readerType = static_cast<ReaderType>(readU32("reader", static_cast<uint32_t>(ReaderType::ModbusRtu)));

  readStr("ap_ssid", SECRET_AP_SSID, cfg.ap.ssid, sizeof(cfg.ap.ssid));
  readStr("ap_pass", SECRET_AP_PASS, cfg.ap.password, sizeof(cfg.ap.password));

  cfg.espNow.channel = static_cast<uint8_t>(readU32("espnow_chan", 6));
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