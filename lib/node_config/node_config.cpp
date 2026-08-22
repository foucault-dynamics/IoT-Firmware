#include "node_config.h"
#include "HardwareSerial.h"

static const struct {
  MeterModel model;
  uint16_t voltage;
  uint16_t import_energy;
  uint16_t export_energy;
} METER_MODELS[] = {
    {MeterModel::Simulated_Serial, /*voltage*/ 0, /*import*/ 4, /*export*/ 2},
    {MeterModel::Simulated_Tcp, 0, 4, 2},
};

// Hard coded
ReaderType loadReaderType() {
  return ReaderType::ModbusRtu;
}

// Hard coded
ModbusRtuConfig loadModbusRtuConfig() {
  const MeterModel model = MeterModel::Simulated_Serial;

  // Unknown model: fall back to the first row so the node still runs.
  // Later application of a hash map (looping through array for now)
  auto entry = METER_MODELS[0];
  for (const auto &candidate : METER_MODELS) {
    if (candidate.model == model) {
      entry = candidate;
      break;
    }
  }

  ModbusRtuConfig cfg{};
  cfg.reader = ReaderType::ModbusRtu;
  cfg.meterModel = model;
  cfg.pollIntervalMs = 1000;

  cfg.bus.rx = 8;
  cfg.bus.tx = 9;
  cfg.bus.dere = 10;
  cfg.bus.baudRate = 9600;
  cfg.bus.format = SERIAL_8N1;

  cfg.slaveAddress = 1;
  cfg.functionCode = 0x03;
  cfg.voltage_address = entry.voltage;
  cfg.import_address = entry.import_energy;
  cfg.export_address = entry.export_energy;

  return cfg;
}
