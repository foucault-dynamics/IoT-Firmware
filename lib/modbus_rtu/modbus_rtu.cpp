#include "modbus_rtu.h"
#include "node_config.h"
#include <Arduino.h>
#include <cstdint>


int ModbusRtuReader::init(Module &module, const ModbusRtuConfig &config){
  this->module = &module;
  this->config = config;

  // Data bits
  float char_len = ((config.bus.format & DATA_BITS_MASK) >> 2) + 5;
  // Parity bits
  if(config.bus.format & PARITY_MASK) char_len++;

  // Stop bit len
  uint8_t stop_bits_flag = ((config.bus.format & STOP_MASK) >> 4);
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
  if(config.bus.baudRate > 19200){
    t35_us = 1750;
  }else{
    t35_us = (uint32_t)((3.5f * char_len * 1000000.0f) / config.bus.baudRate + 0.5f);
  }

  return EXIT_SUCCESS;
}

float ModbusRtuReader::get_import(){
  // Hold while frame pause has not elapsed
  while((micros() - last_rx_us) < t35_us);
  uint8_t request[REQUEST_LEN];
  request[0] = config.slaveAddress;
  request[1] = config.functionCode;
  request[2] = (config.import_address & 0xFF00) >> 8;
  request[3] = config.import_address & 0x00FF;
  request[4] = 0x00;
  request[5] = 0x20;
  uint16_t crc = modbus_crc(request,6);
  request[6] = crc & 0x00FF;
  request[7] = (crc & 0xFF00) >> 8;

  module->send(request,REQUEST_LEN);
  
}

float ModbusRtuReader::get_export(){}

float ModbusRtuReader::get_voltage(){}

uint16_t ModbusRtuReader::modbus_crc(const uint8_t *data, size_t len){
  uint16_t crc = 0xFFFF;
  for(size_t i = 0; i < len; i++){
    crc ^= data[i];
    for(int  b = 0; b < 8; b++){
      if(crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc = crc >> 1;
    }
  }
  return crc;
}
