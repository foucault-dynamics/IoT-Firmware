/**
 * @file
 * Sp3485 implementation.
 */

#include "sp3485.h"
#include "bus_checks.h"
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

int Sp3485::probe(){
  pinMode(derePin,OUTPUT);

  digitalWrite(derePin,LOW);
  PinDrive drive = probePin(RX);
  if(drive != PinDrive::DrivenHigh && drive != PinDrive::DrivenLow){
    Serial.println("[SP3485] RX not driven in receive, transceiver missing or miswired");
    return EXIT_FAILURE;
  }

  digitalWrite(derePin,HIGH);
  drive = probePin(RX);
  digitalWrite(derePin,LOW);
  if(drive != PinDrive::Floating){
    Serial.println("[SP3485] RX still driven with DE/RE high, DE/RE not wired");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


int Sp3485::init(){
  if(probe() != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  if(uartStartChecked("SP3485",*serial,baudRate,serialConfig,RX,TX,false) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  pinMode(derePin,OUTPUT);
  digitalWrite(derePin,LOW);
  // DE/RE stays low, so the driver is off and the test byte never reaches the bus
  return uartLoopbackTest("SP3485",*serial,uartPortOf(*serial),baudRate,serialConfig,false,false);
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
