/**
 * @file
 * Sp3485 implementation.
 */

#include "sp3485.h"
#include "HardwareSerial.h"
#include "esp32-hal-gpio.h"
#include "rs485_config.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>


Sp3485::Sp3485(Rs485Config config, HardwareSerial &serial){
  this->RX = config.rx;
  this->TX = config.tx;
  this->derePin = config.dere;
  this->baudRate = config.baudRate;
  this->serialConfig = config.format;
  this->serial = &serial;
}

/** Time for RX to settle after its pull changes, in us. */
static const uint32_t PULL_SETTLE_US = 50;

/** Reads @p pin with the pull set by @p mode, once the level has settled. */
static int readWithPull(uint8_t pin, uint8_t mode){
  pinMode(pin,mode);
  delayMicroseconds(PULL_SETTLE_US);
  return digitalRead(pin);
}


int Sp3485::probe(){
  pinMode(derePin,OUTPUT);

  digitalWrite(derePin,LOW);
  int up = readWithPull(RX,INPUT_PULLUP);
  int down = readWithPull(RX,INPUT_PULLDOWN);
  if(up != down){
    Serial.println("[SP3485] RX not driven in receive, transceiver missing or miswired");
    return EXIT_FAILURE;
  }

  digitalWrite(derePin,HIGH);
  up = readWithPull(RX,INPUT_PULLUP);
  down = readWithPull(RX,INPUT_PULLDOWN);
  digitalWrite(derePin,LOW);
  if(up != HIGH || down != LOW){
    Serial.println("[SP3485] RX still driven with DE/RE high, DE/RE not wired");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


int Sp3485::init(){
  if(probe() != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  serial->begin(baudRate,serialConfig,RX,TX);
  pinMode(derePin,OUTPUT);
  digitalWrite(derePin,LOW);
  return EXIT_SUCCESS;
}


int Sp3485::readByte(){
  if(digitalRead(derePin) != LOW){
    pinMode(derePin,INPUT);
    digitalWrite(derePin,LOW);
  }
  if(available()){
    return serial->read();
  }
  else{
    return -1;
  }
}


int Sp3485::send(const uint8_t *data, size_t len){
  drainRX();
  digitalWrite(derePin,HIGH);
  size_t sent = serial->write(data,len);
  flush();
  digitalWrite(derePin,LOW);
  if(sent != len){
    Serial.println("[SP3485] error sending data through UART bus");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


bool Sp3485::available(){
  return serial->available();
}


void Sp3485::flush(){
  serial->flush();
}

void Sp3485::drainRX(){
  while(serial->available()){
    serial->read();
  }
}
