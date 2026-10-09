/**
 * @file
 * ModbusRtuReader, the Reader for Modbus RTU meters.
 */

#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "module.h"
#include "rs485_config.h"
#include "reader.h"
#include <cstdint>

/** Bits of an ESP32 SerialConfig value holding the data bit count, minus 5. */
#define DATA_BITS_MASK 0b1100
/** Bit of an ESP32 SerialConfig value that is set when parity is enabled. */
#define PARITY_MASK 0b10
/** Bits of an ESP32 SerialConfig value holding the stop bit setting. */
#define STOP_MASK 0b110000

/** Request frame length: address, function, start, count, CRC. */
#define REQUEST_LEN 8
/** Response frame length for a 2 register read: address, function, byte count, 4 data bytes, CRC. */
#define RESPONSE_LEN 9
/** Shortest frame worth checking: address, function, CRC. */
#define MIN_FRAME_LEN 4
/** How long to wait for a complete response, in ms. */
#define TIMEOUT 500 // 500ms

/**
 * Modbus RTU reader for meters on RS485, or on TCP for simulator testing.
 *
 * Every value is read as two consecutive registers with function code 0x03,
 * which is the shape every value in the meter register map takes. The 32 bit
 * result is decoded as a scaled integer or an IEEE 754 float depending on
 * RegisterFormat.
 *
 * Frame timing follows the spec: a request is only sent after T3.5 (3.5
 * character times) of bus silence, and T3.5 of silence after the last byte
 * ends a response.
 */
class ModbusRtuReader: public Reader{
 public:
  /**
   * Stores the config. Nothing is sent until a getter is called.
   *
   * @param[in] config  Slave address, register addresses and format, copied.
   */
  ModbusRtuReader(const ModbusRtuConfig &config);

  /**
   * Attaches the bus and works out the T3.5 inter frame gap.
   *
   * The gap comes from the frame format's data, parity and stop bits. Above
   * 19200 baud the spec fixes it at 1750 us.
   *
   * @param[in] module  Bus to poll the meter over, Sp3485 or TcpBus.
   * @retval EXIT_SUCCESS  Ready to read.
   * @retval EXIT_FAILURE  The frame format's stop bit setting was invalid.
   */
  int init(Module &module) override;

  int get_import(float *val) override;
  int get_export(float *val) override;
  int get_voltage(float *val) override;
private:
#ifdef PIO_UNIT_TESTING
  friend class ModbusRtuReaderTest;
#endif

  ModbusRtuConfig config;   ///< Slave address, register addresses and format.
  uint32_t t35_us = 0;      ///< T3.5 inter frame gap, in us. Set by init().
  uint32_t last_rx_us = 0;  ///< micros() timestamp of the last received byte.

  /**
   * Reads one 32 bit value from a register pair and decodes it.
   *
   * Waits out T3.5 since the last response, sends the request, then reads and
   * validates the response.
   *
   * @param[in]  data_type_address  Start address of the register pair.
   * @param[out] val                Decoded value. Untouched on failure.
   * @retval EXIT_SUCCESS  @p val holds the value.
   * @retval EXIT_FAILURE  The send failed, the response was invalid, or the
   *                       register format was unknown.
   */
  int read_register(uint16_t data_type_address, float *val);

  /**
   * Computes the Modbus CRC16 of a buffer.
   *
   * @param[in] data  Bytes to checksum.
   * @param[in] len   Number of bytes in @p data.
   * @return The CRC, to be sent low byte first.
   */
  uint16_t modbus_crc(const uint8_t *data, size_t len);

  /**
   * Builds a "read 2 registers" request frame.
   *
   * @param[out] buffer             At least REQUEST_LEN bytes.
   * @param[in]  data_type_address  Start address of the register pair.
   */
  void build_request(uint8_t *buffer,uint16_t data_type_address);

  /**
   * Reads one response frame and validates it.
   *
   * Checks the length, slave address, CRC, exception flag, function code and
   * byte count, logging which check failed. Updates #last_rx_us.
   *
   * @param[out] buffer  At least RESPONSE_LEN bytes. Holds the frame.
   * @retval EXIT_SUCCESS  A valid 2 register response.
   * @retval EXIT_FAILURE  Timed out, malformed, or the meter sent an
   *                       exception.
   */
  int read_response(uint8_t *buffer);

  /**
   * Logs a Modbus exception code in plain words.
   *
   * @param[in] exception  Exception code from the meter's response.
   */
  void request_exception(uint8_t exception);
};

#endif
