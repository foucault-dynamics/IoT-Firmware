#include "sp3485.h"
#include "HardwareSerial.h"
#include "esp32-hal-gpio.h"
#include "node_config.h"
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

// Deferred initialization
void Sp3485::init(){
  serial->begin(baudRate,serialConfig,RX,TX);
  pinMode(derePin,OUTPUT);
  digitalWrite(derePin,HIGH);  
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
