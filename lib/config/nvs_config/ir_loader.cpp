/**
 * @file
 * Config loader for the IR (optical port) node: its NVS keys and defaults.
 */

#include "nvs_config.h"
#include "nvs_read.h"

#include "secrets.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

namespace {

/**
 * UART RX GPIO for the IR head. Board wiring, never an NVS key.
 *
 * From the EE team's IR_probe_signal_testing rig on the C3 SuperMini, where the
 * phototransistor is on 20 and the IR LED on 21.
 *
 * @todo Check against the PCB.
 */
const uint8_t IR_RX_PIN = 20;
/** UART TX GPIO for the IR head. Board wiring, never an NVS key. */
const uint8_t IR_TX_PIN = 21;

/** IrSim/IrSimTCP.py's default port. 5021 so it can run beside ModbusSimTCP.py on 5020. */
const uint16_t IR_SIM_PORT = 5021;

}  // namespace

IrNodeConfig loadIrNodeConfig() {
  prefs.begin(NVS_NAMESPACE, true);

  IrNodeConfig cfg{};

  // The UID is burned into eFuse at the factory, so it never comes from NVS.
#if CONFIG_IDF_TARGET_ESP32C3
  esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, cfg.uid, sizeof(cfg.uid) * 8);
#endif

  cfg.communityId = static_cast<uint8_t>(readU32("comm_id", 0));
  cfg.unitId = static_cast<uint8_t>(readU32("unit_id", 0));
  // default simulated: real circuit doesn't exist yet
  cfg.headMode = static_cast<IrHeadMode>(readU32("simulate", static_cast<uint32_t>(IrHeadMode::Simulated)));

  readStr("ap_ssid", SECRET_AP_SSID, cfg.ap.ssid, sizeof(cfg.ap.ssid));
  readStr("ap_pass", SECRET_AP_PASS, cfg.ap.password, sizeof(cfg.ap.password));

  cfg.espNow.channel = static_cast<uint8_t>(readU32("espnow_chan", 6));  // match substation's fixed listen channel
  cfg.espNow.sendTimeoutMs = readU32("espnow_to_ms", 100);

  uint8_t defaultMac[] = SECRET_MAC;
  readMac("sub_mac", defaultMac, cfg.substation.mac);

  cfg.iec.pollIntervalMs = readU32("poll_ms", 60000);
  cfg.iec.bus.rx = IR_RX_PIN;
  cfg.iec.bus.tx = IR_TX_PIN;
  cfg.iec.bus.baudRate = 300;       // IEC 62056-21 always starts at 300 baud, not NVS tunable
  cfg.iec.bus.format = SERIAL_7E1;  // fixed by the standard
  // On for the IR circuit (light ON reads HIGH), off for a plain USB to serial
  // adapter wired straight to the pins, as in IrSim/IrSimSerial.py testing.
  cfg.iec.bus.invert = readU32("ir_invert", 1) != 0;

  readStr("tcp_host", SECRET_MODBUS_SIM_HOST, cfg.tcp.host, sizeof(cfg.tcp.host));  // the laptop on the softAP
  cfg.tcp.port = static_cast<uint16_t>(readU32("tcp_port", IR_SIM_PORT));
  cfg.tcp.connectTimeoutMs = 3000;

  prefs.end();
  return cfg;
}
