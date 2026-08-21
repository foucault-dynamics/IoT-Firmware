#include "sp3485.h"
#include "HardwareSerial.h"
#include "pin_config.h"
#include <cstdint>
#include <cstdlib>


// Default constructor
Sp3485::Sp3485() : pinConfig(), baudRate(0), serialConfig(SERIAL_8N1), derePin(0) {
}


// Deferred initialization
void Sp3485::init(UartPinConfig pins, uint32_t baud, SerialConfig serialConfig, uint8_t dere){
  pinConfig = pins;
  baudRate = baud;
  this->serialConfig = serialConfig;
  derePin = dere;
}


// Setup
int Sp3485::setup(){
  // Setup UART controller to listen to specified PINs at given baud rate
  Serial1.begin(baudRate,serialConfig,pinConfig.RX,pinConfig.TX);
  // Setup the DE/RE pin on LOW
  pinMode(derePin,LOW);
  return EXIT_SUCCESS;
}


int Sp3485::receive(uint8_t *buf, size_t maxLen){
  return EXIT_SUCCESS;
}


int Sp3485::send(const uint8_t *data, size_t len){
  return EXIT_SUCCESS;
}


bool Sp3485::available(){
  return false;
}


void Sp3485::flush(){
  // TODO: Serial1.flush();
}
