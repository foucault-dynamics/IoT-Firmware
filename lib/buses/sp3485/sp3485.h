/**
 * @file
 * Sp3485, the Module for the SP3485 RS485 transceiver.
 */

#ifndef SP3485_H
#define SP3485_H

#include "module.h"
#include "rs485_config.h"
#include <cstdint>
#include "HardwareSerial.h"


/**
 * SP3485 RS485 transceiver on a hardware UART.
 *
 * RS485 is half duplex, so the DE/RE pin must be driven high to transmit and
 * dropped back low to receive, and only after the last bit has physically left
 * the UART. Dropping it early cuts off the end of the frame.
 */
class Sp3485 : public Module {
 private:
  uint8_t RX;                 ///< UART RX GPIO.
  uint8_t TX;                 ///< UART TX GPIO.
  uint32_t baudRate;          ///< Bus speed in baud.
  SerialConfig serialConfig;  ///< Frame format, e.g. SERIAL_8N1.
  uint8_t derePin;            ///< GPIO driving DE/RE. High transmits, low receives.
  HardwareSerial *serial;     ///< UART the transceiver is wired to.

  /** Blocks until the TX buffer has fully left the wire. */
  void flush();
  /** Discards every byte waiting in the RX buffer. */
  void drainRX();
  /**
   * Checks the transceiver is fitted and DE/RE reaches it, before the UART
   * claims the RX pin.
   *
   * Reads RX once with the internal pull up and once with the pull down. With
   * DE/RE low the receiver drives RX, so both reads agree. With DE/RE high its
   * output goes high impedance, so RX follows the pull. Comparing reads, not
   * expecting a fixed level, keeps this independent of the bus state.
   *
   * @retval EXIT_SUCCESS  RX is driven in receive and floats when DE/RE is high.
   * @retval EXIT_FAILURE  RX floats in receive (transceiver missing, unpowered
   *                       or RO not wired to RX), or stays driven with DE/RE
   *                       high (DE/RE not wired).
   */
  int probe();

 public:
  /**
   * Stores the pins and UART. Nothing is started until init().
   *
   * @param[in] config  Pins, baud rate and frame format.
   * @param[in] serial  Hardware UART the transceiver is wired to. Must outlive
   *                    this object.
   */
  Sp3485(Rs485Config config, HardwareSerial &serial);

  /**
   * Probes the transceiver, starts the UART, sets up the DE/RE pin as an
   * output driven low so the node idles listening, then checks the UART with
   * a loopback test. DE/RE stays low through the test, so the driver is off
   * and nothing reaches the bus.
   *
   * @retval EXIT_SUCCESS  The probe passed, the UART is running and its
   *                       loopback echoed.
   * @retval EXIT_FAILURE  The probe failed (see probe()), the UART driver did
   *                       not start, or the loopback echo was missing or
   *                       wrong.
   */
  int init() override;

  /**
   * Reads one received byte, switching DE/RE to receive first if needed.
   *
   * @return The byte (0 to 255), or -1 if nothing is waiting.
   */
  int readByte() override;

  /**
   * Sends one frame: drops stale RX bytes, drives DE/RE high, writes, waits
   * for the last bit to leave, then drops DE/RE low.
   *
   * Draining RX first stops a late reply to a timed out poll from being read
   * as the start of the next frame.
   *
   * @param[in] data  Frame bytes.
   * @param[in] len   Number of bytes in @p data.
   * @retval EXIT_SUCCESS  Every byte was written.
   * @retval EXIT_FAILURE  The UART accepted fewer than @p len bytes.
   */
  int send(const uint8_t *data, size_t len) override;

  bool available() override;
};

#endif
