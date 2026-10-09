/**
 * @file
 * Config loader for the RS485 node: its NVS keys, defaults and meter models.
 */

#include "nvs_config.h"
#include "nvs_read.h"

#include <cstring>

#include "secrets.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

namespace {

/** UART RX GPIO for the SP3485. Board wiring, never an NVS key. */
const uint8_t RS485_RX_PIN = 8;
/** UART TX GPIO for the SP3485. Board wiring, never an NVS key. */
const uint8_t RS485_TX_PIN = 9;
/** GPIO driving the SP3485's DE/RE pins. Board wiring, never an NVS key. */
const uint8_t RS485_DERE_PIN = 10;

/** OBIS code of total imported active energy, 1.0.1.8.0.255. */
const uint8_t DLMS_IMPORT_OBIS[6] = {1, 0, 1, 8, 0, 255};
/** OBIS code of total exported active energy, 1.0.2.8.0.255. */
const uint8_t DLMS_EXPORT_OBIS[6] = {1, 0, 2, 8, 0, 255};
/** OBIS code of the phase 1 instantaneous voltage, 1.0.32.7.0.255. */
const uint8_t DLMS_VOLTAGE_OBIS[6] = {1, 0, 32, 7, 0, 255};

/** Register map of one supported meter model. */
struct MeterModelEntry {
  MeterModel model;        ///< Model this row describes.
  uint16_t voltage;        ///< Start address of the voltage register pair.
  uint16_t import_energy;  ///< Start address of the import energy register pair.
  uint16_t export_energy;  ///< Start address of the export energy register pair.
};

/**
 * Register maps for every MeterModel, picked by the `meter_model` NVS key.
 *
 * @todo Find a better way to describe meter models than a hardcoded table.
 */
const MeterModelEntry METER_MODELS[] = {
    {MeterModel::Simulated_Serial, /*voltage*/ 0, /*import*/ 4, /*export*/ 2},
    {MeterModel::Simulated_Tcp, 0, 4, 2},
};

/**
 * Finds the register map for a meter model.
 *
 * @param[in] model  Model to look up.
 * @return Its row in METER_MODELS, or the first row if the model is unknown.
 */
const MeterModelEntry &lookupMeterModel(MeterModel model) {
  for (const auto &entry : METER_MODELS) {
    if (entry.model == model) {
      return entry;
    }
  }
  return METER_MODELS[0];
}

}  // namespace

Rs485NodeConfig loadRs485NodeConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  Rs485NodeConfig cfg{};

  // The UID is burned into eFuse at the factory, so it never comes from NVS.
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

  cfg.dlms.bus = cfg.modbus.bus;
  cfg.dlms.clientSap = static_cast<uint8_t>(readU32("dlms_client", 16));
  cfg.dlms.serverLogical = static_cast<uint16_t>(readU32("dlms_logical", 1));
  cfg.dlms.serverPhysical = static_cast<uint16_t>(readU32("dlms_physical", 0));
  cfg.dlms.serverAddrLen = static_cast<uint8_t>(readU32("dlms_addr_len", 1));
  memcpy(cfg.dlms.importObis, DLMS_IMPORT_OBIS, sizeof(DLMS_IMPORT_OBIS));
  memcpy(cfg.dlms.exportObis, DLMS_EXPORT_OBIS, sizeof(DLMS_EXPORT_OBIS));
  memcpy(cfg.dlms.voltageObis, DLMS_VOLTAGE_OBIS, sizeof(DLMS_VOLTAGE_OBIS));

  readStr("tcp_host", SECRET_MODBUS_SIM_HOST, cfg.tcp.host, sizeof(cfg.tcp.host));
  cfg.tcp.port = static_cast<uint16_t>(readU32("tcp_port", SECRET_MODBUS_SIM_PORT));
  cfg.tcp.connectTimeoutMs = 3000;

  prefs.end();
  return cfg;
}
