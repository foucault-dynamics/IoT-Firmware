#include "modbus_rtu.h"
#include "esp32-hal.h"
#include "node_config.h"
#include <Arduino.h>
#include <cstdint>
#include <cstdlib>


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

int ModbusRtuReader::get_import(float *val){
  // Hold while frame pause has not elapsed
  while((micros() - last_rx_us) < t35_us);
  uint8_t request[REQUEST_LEN];
  build_request(request,config.import_address);

  // Send
  if(module->send(request,REQUEST_LEN) == EXIT_FAILURE){
    Serial.println("[ModbusRTU Reader] Error with bus\n");
    return EXIT_FAILURE;
  }
  last_rx_us = micros();  
  
  // Wait for response to be fully received
  while((micros() - last_rx_us) < t35_us);
  
  uint8_t response[RESPONSE_LEN];
  uint32_t lastByteTime = micros();
  uint32_t startTime = millis();
  size_t index = 0;
  int capture;

 read:
  // Read response
  while(millis() - startTime < TIMEOUT){
    if(index < RESPONSE_LEN){
      capture = module->readByte();
      if(capture == -1){
	Serial.println("[ModbusRTU Reader] message incomplete");
	return EXIT_FAILURE;
      }
      response[index++] = capture;      
    }else{
      if(module->readByte() == -1){
	break;
      }
    }
  }

  if(index != RESPONSE_LEN-1){
    Serial.println("[ModbusRTU Reader] Response incomplete");
    return EXIT_FAILURE;
  }

  if(response[0] != config.slaveAddress){
    Serial.println("[ModbusRTU Reader] Slave address response does not match");
    return EXIT_FAILURE;
  }
  if(response[1] & 0x80){
    uint8_t originalFunctionCode = response[1] & 0x7F;
    uint8_t fatal = request_exception(response[2]);
    if(!fatal){
      goto read;
    }
    return EXIT_FAILURE;
  }
  if(response[1] != config.functionCode){
    Serial.println("[ModbusRTU Reader] Function code response does not match");
    return EXIT_FAILURE;
  }
  
  uint8_t bytes = response[2];
  if(bytes != 4){
    Serial.println("[ModbusRTU Reader] Expected byte length mismatch");
    return EXIT_FAILURE;
  }
  if(modbus_crc(response,3+bytes) != response[2+bytes]){
    Serial.println("[ModbusRTU Reader] CRC does not match possible corruption in line");
    return EXIT_FAILURE;
  }
  
  //Return value captured
  *val = (response[3] << 24 | response[4] << 16 | response[5] << 8 | response[6]);
    
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


void ModbusRtuReader::build_request(uint8_t *buffer, uint32_t data_type_address){
  buffer[0] = config.slaveAddress;
  buffer[1] = config.functionCode;
  buffer[2] = (data_type_address & 0xFF00) >> 8;
  buffer[3] = data_type_address & 0x00FF;
  buffer[4] = 0x00;
  buffer[5] = 0x02;
  uint16_t request_crc = modbus_crc(buffer,6);
  buffer[6] = request_crc & 0x00FF;
  buffer[7] = (request_crc & 0xFF00) >> 8;        
}


uint8_t ModbusRtuReader::request_exception(uint8_t exception){
  switch(exception){
  case 0x01:
    Serial.println("[ModbusRTU Reader] Illegal function passed");
    return 1;
  case 0x02:
    Serial.println("[ModbusRTU Reader] Illegal Data Address");
    return 1;
  case 0x03:
    Serial.println("[ModbusRTU Reader] Illegal data value");
    return 1;
  case 0x04:
    Serial.println("[ModbusRTU Reader] Slave device failure");
    return 1;
  case 0x05:
    Serial.println("[ModbusRTU Reader] Slave needed more time to respond");
    return 0;
  case 0x06:
    Serial.println("[ModbusRTU Reader] Slave busy");
    return 0;
  }
}
