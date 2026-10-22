/**
 * @file
 * Bring up checks shared by the UART buses: pin drive probes, a checked UART
 * start and a UART loopback test.
 */

#ifndef BUS_CHECKS_H
#define BUS_CHECKS_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <driver/uart.h>
#include <cstdint>

/** What is holding a pin, worked out by reading it under each internal pull. */
enum class PinDrive : uint8_t {
  DrivenHigh,  ///< Reads HIGH under both pulls, so something drives it high.
  DrivenLow,   ///< Reads LOW under both pulls, so something drives it low.
  Floating,    ///< Follows each pull, so nothing drives it.
  Unstable     ///< Reads LOW under the pull up and HIGH under the pull down,
               ///< so the source is toggling or too weak to trust.
};

/**
 * Classifies a pin from its level under each internal pull.
 *
 * @param[in] up    Level read with INPUT_PULLUP, HIGH or LOW.
 * @param[in] down  Level read with INPUT_PULLDOWN, HIGH or LOW.
 * @return (1,1) DrivenHigh, (0,0) DrivenLow, (1,0) Floating, (0,1) Unstable.
 */
PinDrive classifyPin(int up, int down);

/**
 * Reads a pin with an internal pull applied, once the level has settled.
 *
 * @param[in] pin   GPIO to read.
 * @param[in] mode  INPUT_PULLUP or INPUT_PULLDOWN.
 * @return The pin's level, HIGH or LOW.
 */
int readWithPull(uint8_t pin, uint8_t mode);

/**
 * Reads a pin under the pull up then the pull down, and classifies it.
 *
 * Leaves the pin as an input with the pull down. Call it before the UART
 * claims the pin.
 *
 * @param[in] pin  GPIO to probe.
 * @return How the pin is driven, see classifyPin().
 */
PinDrive probePin(uint8_t pin);

/**
 * Says whether a UART RX drive is the line's idle level.
 *
 * A UART line idles HIGH, or LOW when its polarity is inverted. Only the idle
 * level driven by the device counts: a floating or unstable line means nothing
 * trustworthy is on the other end.
 *
 * @param[in] drive     How RX is driven, from probePin().
 * @param[in] rxInvert  True when the UART reads RX inverted.
 * @retval true   @p drive is the idle level for that polarity.
 * @retval false  Anything else.
 */
bool rxDriveIsIdle(PinDrive drive, bool rxInvert);

/**
 * Checks a device is driving a UART RX pin at its idle level, before the UART
 * claims the pin.
 *
 * @param[in] tag       Log prefix, e.g. "RealIrHead".
 * @param[in] pin       RX GPIO.
 * @param[in] rxInvert  True when the UART reads RX inverted, so idle is LOW.
 * @retval EXIT_SUCCESS  RX is driven at the idle level.
 * @retval EXIT_FAILURE  RX floats (device missing or unpowered), is held at
 *                       the active level (light on the sensor, or the wrong
 *                       polarity), or is unstable. The reason is logged.
 */
int rxIdleCheck(const char *tag, uint8_t pin, bool rxInvert);

/**
 * Starts a UART and checks its driver really came up.
 *
 * HardwareSerial::begin() returns nothing, and on a bad pin or a failed driver
 * install it just logs and leaves the UART stopped. Its bool operator reports
 * whether the driver is installed, so that is checked straight after. The pins
 * are checked first, because core 2.0.17 panics, rather than failing, when
 * begin() cannot attach a pin.
 *
 * @param[in] tag     Log prefix, e.g. "SP3485".
 * @param[in] serial  UART to start.
 * @param[in] baud    Rate in baud.
 * @param[in] format  Frame format, e.g. SERIAL_8N1.
 * @param[in] rx      RX GPIO.
 * @param[in] tx      TX GPIO.
 * @param[in] invert  Flips both lines' polarity, as begin() does.
 * @retval EXIT_SUCCESS  The UART driver is running.
 * @retval EXIT_FAILURE  @p rx is not a GPIO, @p tx is not an output GPIO, or
 *                       the driver did not start.
 */
int uartStartChecked(const char *tag, HardwareSerial &serial, uint32_t baud,
                     SerialConfig format, uint8_t rx, uint8_t tx, bool invert);

/**
 * Finds which hardware UART a HardwareSerial object drives.
 *
 * HardwareSerial keeps its UART number private, so this matches the object's
 * address against the core's UART0 and UART1 objects.
 *
 * @param[in] serial  UART object.
 * @return UART_NUM_0 or UART_NUM_1, or UART_NUM_MAX for any other object.
 */
uart_port_t uartPortOf(const HardwareSerial &serial);

/**
 * Sends one byte through the UART's internal loopback and checks it comes back
 * unchanged.
 *
 * This proves the driver, the baud rate and the frame format work end to end
 * inside the chip, without anything on the pins answering. RX is set to TX's
 * polarity for the test so a head with split polarity still echoes cleanly,
 * and loopback, RX's polarity and an empty RX buffer are restored on every
 * exit.
 *
 * @param[in] tag       Log prefix, e.g. "SP3485".
 * @param[in] serial    Started UART.
 * @param[in] port      @p serial's UART number, from uartPortOf().
 * @param[in] baud      Rate @p serial runs at, for the timeouts.
 * @param[in] format    Frame format @p serial runs, for the timeouts.
 * @param[in] txInvert  TX polarity @p serial runs with.
 * @param[in] rxInvert  RX polarity to restore afterwards.
 * @retval EXIT_SUCCESS  Exactly the byte sent came back.
 * @retval EXIT_FAILURE  @p port is unknown, nothing came back, or something
 *                       else did.
 */
int uartLoopbackTest(const char *tag, HardwareSerial &serial, uart_port_t port,
                     uint32_t baud, SerialConfig format, bool txInvert, bool rxInvert);

#endif
