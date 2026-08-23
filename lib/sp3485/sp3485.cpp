#include "sp3485.h"
#include "HardwareSerial.h"
#include "pin_config.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>


// Deferred initialization
void Sp3485::init(uint8_t RX, uint8_t TX, uint8_t DERE, uint32_t baud, SerialConfig serialConfig){
  this->RX = RX;
  this->TX = TX;
  this->derePin = DERE;
  this->baudRate = baud;
  this->serialConfig = serialConfig;
}


// Setup
int Sp3485::setup(){
  // Setup UART controller to listen to specified PINs at given baud rate
  Serial1.begin(baudRate,serialConfig,RX,TX);
  // Setup the DE/RE pin on LOW
  pinMode(derePin,INPUT);
  digitalWrite(derePin,LOW);
  return EXIT_SUCCESS;
}


int Sp3485::readByte(){
  if(digitalRead(derePin) != HIGH){
    pinMode(derePin,OUTPUT);
    digitalWrite(derePin,HIGH);
  }
  if(available()){
    return Serial1.read();
  }
  else{
    return -1;
  }
}


int Sp3485::send(const uint8_t *data, size_t len){
  digitalWrite(derePin,HIGH);
  size_t sent = Serial1.write(data,len); 
  flush();
  digitalWrite(derePin,LOW);  
  if(sent != len){
    Serial.println("[SP3485] error sending data through UART bus");    
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


bool Sp3485::available(){
  return Serial1.available();
}


void Sp3485::flush(){
  // TODO: Serial1.flush();
  Serial1.flush();
}
