/**
 * @file
 * RealIrHead, the IrHead for the EE team's UART to IR converter.
 */

#ifndef REAL_IR_HEAD_H
#define REAL_IR_HEAD_H

#include "ir_head.h"
#include "ir_config.h"
#include "HardwareSerial.h"

/**
 * The EE team's UART to IR converter, wired to one of the ESP32-C3's hardware
 * UARTs.
 *
 * From the firmware's side this is a plain UART. The IR modulation and
 * demodulation happen entirely inside the EE team's circuit. Which UART it is
 * wired to (Serial1 and so on) is up to the caller, the same pattern Sp3485
 * uses.
 *
 * Tested with the PiggyMeter head against IrSim/board_meter over real light:
 * the 300 baud handshake works every time, the 9600 baud data block only
 * sometimes, so the head may need a lower rate.
 */
class RealIrHead : public IrHead {
 private:
  uint8_t RX;                 ///< UART RX GPIO.
  uint8_t TX;                 ///< UART TX GPIO.
  uint32_t baudRate;          ///< Current rate in baud, changed by setBaudRate().
  SerialConfig serialConfig;  ///< Frame format, 7E1 for IEC 62056-21.
  bool txInvert;              ///< Flip TX polarity, see IrConfig::txInvert.
  bool rxInvert;              ///< Flip RX polarity, see IrConfig::rxInvert.
  HardwareSerial *serial;     ///< UART the converter is wired to.

  /** (Re)starts the UART at #baudRate with each line's polarity. */
  void start();

 public:
  /**
   * Stores the pins and UART. The UART is not started until init().
   *
   * @param[in] config  Pins, starting baud rate, frame format and polarity.
   * @param[in] serial  Hardware UART the converter is wired to. Must outlive
   *                    this object.
   */
  RealIrHead(IrConfig config, HardwareSerial &serial);

  int init() override;
  int send(const uint8_t *data, size_t len) override;
  int readByte() override;
  bool available() override;
  void setBaudRate(uint32_t baud) override;
};

#endif
