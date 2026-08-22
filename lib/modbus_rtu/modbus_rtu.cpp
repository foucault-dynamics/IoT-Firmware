#include "modbus_rtu.h"
#include "node_config.h"
#include <Arduino.h>
#include <cstdint>


int ModbusRtuReader::init(Module &module,NodeConfig &nodeConfig){
  this->module = &module;
  
  if(nodeConfig.reader != ReaderType::ModbusRtu){
    Serial.printf("[ModbusRtuReader] init() got wrong config type: reader=%d\n",(int)nodeConfig.reader);
    return EXIT_FAILURE; 
  }  
  this->config = static_cast<ModbusRtuConfig &>(nodeConfig);

  // Data bits
  uint8_t char_len = ((config.serialConfig & DATA_BITS_MASK) >> 2) + 5;
  // Parity bits
  if(config.serialConfig & PARITY_MASK) char_len++;

  // Stop bit len
  uint8_t stop_bits_flag = ((config.serialConfig & STOP_MASK) >> 4);  
  switch(stop_bits_flag){
  case 0b01:
    char_len++;
    break;
  case 0b10:
    char_len += 1.5;
    break;
  case 0b11:
    char_len += 2;
    break;
  default:
    Serial.println("[ModbusRtuReader] error reading stop bits from config");
    return EXIT_FAILURE;
  }
  // Start bit
  char_len++;

  //Space-between frames
  if(config.baudRate > 19200){
    t35_us = 1750;
  }else{
    t35_us = (uint32_t)((3.5f * char_len * 1000000.0f) / config.baudRate + 0.5f);
  }

  return EXIT_SUCCESS;
}

float ModbusRtuReader::get_import(){
  // Set pin high for sending
  pinMode(config.DERE,OUTPUT);
  
}

float ModbusRtuReader::get_export(){}

float ModbusRtuReader::get_voltage(){}

