/**
 * @file
 * Config structs and enums for the RS485 node, Modbus RTU and DLMS/COSEM.
 */

#ifndef RS485_CONFIG_H
#define RS485_CONFIG_H

#include <cstdint>
#include "HardwareSerial.h"
#include "networking_config.h"

/** Which protocol a reader node speaks on its bus. Stored in NVS as a number. */
enum class ReaderType : uint8_t {
  ModbusRtu = 0,  ///< Modbus RTU over the SP3485 RS485 transceiver.
    Iec62056 = 1,  ///< IEC 62056-21. Not handled by the RS485 node.
    IEMS = 2,  ///< IEMS reader. Not implemented on this branch.
    ModbusTCP = 3,  ///< Modbus RTU frames over TCP, for ModbusSim testing.
    CamHttp = 4,  ///< ESP32-CAM over HTTP. Not handled by the RS485 node.
    DlmsCosem = 5,  ///< DLMS/COSEM over HDLC on the SP3485 RS485 transceiver.
    DlmsTcp = 6  ///< DLMS/COSEM HDLC frames over TCP, for DlmsSim testing.
};
/**
 * Which meter a reader node is attached to.
 *
 * Each value has a row in the loader's METER_MODELS table giving its register
 * addresses.
 */
enum class MeterModel : uint8_t {
  Simulated_Serial = 0,  ///< ModbusSimSerial.py register map.
    Simulated_Tcp = 1,  ///< ModbusSimTCP.py register map.
};

/** How a meter encodes a 32 bit value across two Modbus registers. */
enum class RegisterFormat : uint8_t{
  ScaledInt = 0,  ///< Unsigned integer in thousandths (Wh for a kWh reading).
    IEEE_754Float = 1,  ///< IEEE 754 single precision float, big endian.
};

/** UART and direction pin settings for the SP3485 transceiver. */
struct Rs485Config {
  /** UART RX and TX GPIOs, and the GPIO driving the transceiver's DE/RE pins. */
  ///@{
  uint8_t rx, tx, dere;
  ///@}
  uint32_t baudRate;    ///< Bus speed in baud.
  SerialConfig format;  ///< Frame format, e.g. SERIAL_8N1.
};

/** Where TcpBus connects to reach the Modbus TCP simulator. */
struct TcpBusConfig {
  char host[64];              ///< Simulator's IP address or hostname.
  uint16_t port;              ///< Simulator's TCP port.
  uint32_t connectTimeoutMs;  ///< Timeout for one connect attempt, in ms.
};

/** Modbus RTU reader settings: which device, which registers, how to decode. */
struct ModbusRtuConfig {
  MeterModel meterModel;  ///< Meter the register addresses below came from.
  /**
   * Intended time between readings, in ms.
   *
   * @todo rs485NodeLoop() ignores this and always waits POLL_INTERVAL_MS.
   */
  uint32_t pollIntervalMs;

  Rs485Config bus;  ///< Transceiver settings.

  RegisterFormat registerFormat;  ///< How register pairs are decoded.
  uint8_t slaveAddress;           ///< Modbus address of the meter.
  uint8_t functionCode;           ///< Read function code, 0x03 (holding registers).
  /** Start address of the voltage, import and export register pairs. */
  ///@{
  uint16_t voltage_address, import_address, export_address;
  ///@}
};

/** DLMS/COSEM reader settings: who the node is, which meter, which objects. */
struct DlmsCosemConfig {
  Rs485Config bus;          ///< Transceiver settings.
  uint8_t clientSap;        ///< Client address, 16 for the public client.
  uint16_t serverLogical;   ///< Server logical device, 1 for management.
  uint16_t serverPhysical;  ///< Meter's physical address on the bus.
  uint8_t serverAddrLen;    ///< Server address size in bytes, 1, 2 or 4.
  /** OBIS codes A.B.C.D.E.F of the import, export and voltage registers. */
  ///@{
  uint8_t importObis[6], exportObis[6], voltageObis[6];
  ///@}
};

/** Everything the RS485 node needs, filled in by loadRs485NodeConfig(). */
struct Rs485NodeConfig {
  uint8_t uid[16];              ///< This board's eFuse UID.
  uint8_t communityId;          ///< Where the node is installed. Set from NVS, not hardware.
  uint8_t unitId;               ///< Unit within the community. Set from NVS, not hardware.
  ReaderType readerType;        ///< Which bus and reader to build.
  SoftApConfig ap;              ///< AP the simulator host joins, ModbusTCP only.
  EspNowConfig espNow;          ///< Uplink to the substation.
  EspNowPeerConfig substation;  ///< Substation to send readings to.
  ModbusRtuConfig modbus;       ///< Reader settings.
  TcpBusConfig tcp;             ///< Simulator connection, ModbusTCP only.
  DlmsCosemConfig dlms;         ///< DLMS/COSEM reader settings.
};

#endif
