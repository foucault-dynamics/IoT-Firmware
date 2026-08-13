#include "uart.h"

// TODO: implement over HardwareSerial (Serial1).
//
// Uart::Uart(pins, baud) : pinConfig(pins), baudRate(baud) {}
//
// setup()     = Serial1.begin(baudRate, SERIAL_8N1, pinConfig.RX, pinConfig.TX);
// send()      = Serial1.write(data, len);
// receive()   = Serial1.readBytes(buf, min(maxLen, Serial1.available()));
// available() = Serial1.available() > 0;
// flush()     = Serial1.flush();
