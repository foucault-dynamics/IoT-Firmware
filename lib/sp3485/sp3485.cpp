#include "sp3485.h"

// TODO: implement on top of Uart.
//
// Sp3485::Sp3485(pins, baud, dere) : Uart(pins, baud), derePin(dere) {}
//
// setup()  = Uart::setup();
//            pinMode(derePin, OUTPUT);
//            digitalWrite(derePin, LOW);   // idle in receive
//
// send()   = digitalWrite(derePin, HIGH);  // take the bus
//            int n = Uart::send(data, len);
//            flush();                      // MUST block until the last bit
//                                          // is out; dropping DE early
//                                          // truncates the final byte
//            digitalWrite(derePin, LOW);   // release the bus
//            return n;
//
// receive() and available() are inherited from Uart unchanged.
