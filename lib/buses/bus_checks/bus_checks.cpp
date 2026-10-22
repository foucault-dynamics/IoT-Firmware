/**
 * @file
 * Bus check implementations.
 */

#include "bus_checks.h"
#include <driver/gpio.h>
#include <cstdlib>

/** Time for a pin to settle after its pull changes, in us. */
static const uint32_t PULL_SETTLE_US = 50;

/** Byte sent through the loopback. Alternating bits catch a wrong baud rate. */
static const uint8_t LOOPBACK_BYTE = 0x55;
/** Shortest wait for the loopback echo, in ms. */
static const uint32_t LOOPBACK_MIN_WAIT_MS = 50;
/** Character times to wait for the loopback echo before giving up. */
static const uint32_t LOOPBACK_WAIT_CHARS = 20;
/** Character times to keep listening after the echo, for stray extra bytes. */
static const uint32_t LOOPBACK_TAIL_CHARS = 2;


PinDrive classifyPin(int up, int down){
  if(up == HIGH && down == HIGH) return PinDrive::DrivenHigh;
  if(up == LOW && down == LOW) return PinDrive::DrivenLow;
  if(up == HIGH && down == LOW) return PinDrive::Floating;
  return PinDrive::Unstable;
}


int readWithPull(uint8_t pin, uint8_t mode){
  pinMode(pin,mode);
  delayMicroseconds(PULL_SETTLE_US);
  return digitalRead(pin);
}


PinDrive probePin(uint8_t pin){
  int up = readWithPull(pin,INPUT_PULLUP);
  int down = readWithPull(pin,INPUT_PULLDOWN);
  return classifyPin(up,down);
}


bool rxDriveIsIdle(PinDrive drive, bool rxInvert){
  return drive == (rxInvert ? PinDrive::DrivenLow : PinDrive::DrivenHigh);
}


int rxIdleCheck(const char *tag, uint8_t pin, bool rxInvert){
  PinDrive drive = probePin(pin);
  if(rxDriveIsIdle(drive,rxInvert)){
    return EXIT_SUCCESS;
  }
  switch(drive){
  case PinDrive::Floating:
    Serial.printf("[%s] RX floats, head missing or unpowered\n",tag);
    break;
  case PinDrive::Unstable:
    Serial.printf("[%s] RX unstable\n",tag);
    break;
  default:
    Serial.printf("[%s] RX held active: light on sensor or wrong ir_rx_inv\n",tag);
    break;
  }
  return EXIT_FAILURE;
}


int uartStartChecked(const char *tag, HardwareSerial &serial, uint32_t baud,
                     SerialConfig format, uint8_t rx, uint8_t tx, bool invert){
  // Checked here because core 2.0.17 dereferences a null pointer in its own
  // error log when begin() fails to attach a pin, which panics the chip
  if(!GPIO_IS_VALID_GPIO(rx) || !GPIO_IS_VALID_OUTPUT_GPIO(tx)){
    Serial.printf("[%s] UART pins invalid (RX %u, TX %u)\n",tag,rx,tx);
    return EXIT_FAILURE;
  }
  serial.begin(baud,format,rx,tx,invert);
  if(!serial){
    Serial.printf("[%s] UART failed to start\n",tag);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


uart_port_t uartPortOf(const HardwareSerial &serial){
  if(&serial == &Serial1) return UART_NUM_1;
  // With USB CDC on boot, Serial is the USB port and Serial0 is UART0
#if ARDUINO_USB_CDC_ON_BOOT
  if(&serial == &Serial0) return UART_NUM_0;
#else
  if(&serial == &Serial) return UART_NUM_0;
#endif
  return UART_NUM_MAX;
}


/**
 * Works out how long one character takes on the wire, the same way
 * ModbusRtuReader::init() does: start bit, data bits, parity and stop bits.
 *
 * @param[in] baud    Rate in baud.
 * @param[in] format  Frame format, e.g. SERIAL_8N1.
 * @return One character time in us, rounded up.
 */
static uint32_t charTimeUs(uint32_t baud, SerialConfig format){
  uint32_t bits = 1 + (((format & 0b1100) >> 2) + 5);
  if(format & 0b10) bits++;
  // 1.5 stop bits rounds up to 2, this only sets timeouts
  bits += (((format & 0b110000) >> 4) == 0b01) ? 1 : 2;
  return (bits * 1000000UL + baud - 1) / baud;
}

/**
 * Discards every byte waiting in a UART's RX buffer.
 *
 * @param[in] serial  UART to drain.
 */
static void drain(HardwareSerial &serial){
  while(serial.available()){
    serial.read();
  }
}

/**
 * Turns loopback off, restores RX's polarity and drops anything the test left
 * behind.
 *
 * @param[in] serial    UART under test.
 * @param[in] port      Its UART number.
 * @param[in] rxInvert  RX polarity to restore.
 */
static void endLoopback(HardwareSerial &serial, uart_port_t port, bool rxInvert){
  uart_set_loop_back(port,false);
  serial.setRxInvert(rxInvert);
  drain(serial);
}


int uartLoopbackTest(const char *tag, HardwareSerial &serial, uart_port_t port,
                     uint32_t baud, SerialConfig format, bool txInvert, bool rxInvert){
  if(port == UART_NUM_MAX){
    Serial.printf("[%s] Unknown UART\n",tag);
    return EXIT_FAILURE;
  }

  uint32_t charUs = charTimeUs(baud,format);
  uint32_t waitMs = (LOOPBACK_WAIT_CHARS * charUs + 999) / 1000;
  if(waitMs < LOOPBACK_MIN_WAIT_MS) waitMs = LOOPBACK_MIN_WAIT_MS;

  drain(serial);
  // Match RX to TX so a head with split polarity still reads its own echo
  serial.setRxInvert(txInvert);
  uart_set_loop_back(port,true);
  serial.write(LOOPBACK_BYTE);

  uint32_t start = millis();
  while(!serial.available() && millis() - start < waitMs){
    delay(1);
  }
  if(!serial.available()){
    endLoopback(serial,port,rxInvert);
    Serial.printf("[%s] UART loopback: no echo\n",tag);
    return EXIT_FAILURE;
  }

  // Give any stray second byte time to land
  delayMicroseconds(LOOPBACK_TAIL_CHARS * charUs);
  int first = serial.read();
  int extra = serial.available() ? serial.read() : -1;
  endLoopback(serial,port,rxInvert);

  if(first != LOOPBACK_BYTE){
    Serial.printf("[%s] UART loopback: got 0x%02X\n",tag,first);
    return EXIT_FAILURE;
  }
  if(extra != -1){
    Serial.printf("[%s] UART loopback: got 0x%02X\n",tag,extra);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
