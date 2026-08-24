#include "modbus_rtu.h"
#include "esp32-hal.h"
#include "node_config.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>


int ModbusRtuReader::init(Module &module, const void *config){
  this->module = &module;
  this->config = static_cast<const ModbusRtuConfig *>(config);

  // Data bits
  float char_len = ((this->config->bus.format & DATA_BITS_MASK) >> 2) + 5;
  // Parity bits
  if(this->config->bus.format & PARITY_MASK) char_len++;

  // Stop bit len
  uint8_t stop_bits_flag = ((this->config->bus.format & STOP_MASK) >> 4);
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
  if(this->config->bus.baudRate > 19200){
    t35_us = 1750;
  }else{
    t35_us = (uint32_t)((3.5f * char_len * 1000000.0f) / this->config->bus.baudRate + 0.5f);
  }

  return EXIT_SUCCESS;
}

int ModbusRtuReader::get_import(float *val){
  return read_register(config->import_address,val);
}

int ModbusRtuReader::get_export(float *val){
  return read_register(config->export_address,val);
}

int ModbusRtuReader::get_voltage(float *val){
  return read_register(config->voltage_address,val);
}


int ModbusRtuReader::read_register(uint16_t data_type_address, float *val){
  // Hold while frame pause has not elapsed
  while((micros() - last_rx_us) < t35_us);

  //Send Request
  uint8_t request[REQUEST_LEN];
  build_request(request,data_type_address);
  if(module->send(request,REQUEST_LEN) == EXIT_FAILURE){
    Serial.println("[ModbusRTU Reader] Error with bus\n");
    return EXIT_FAILURE;
  }

  //Capture Response
  uint8_t response[RESPONSE_LEN];
  if(read_response(response) == EXIT_FAILURE){
    return EXIT_FAILURE;
  }

  uint32_t raw = (uint32_t)response[3] << 24 | (uint32_t)response[4] << 16 | (uint32_t)response[5] << 8 | (uint32_t)response[6];
  if(config->registerFormat == RegisterFormat::IEEE_754Float){
    memcpy(val,&raw,sizeof(float));
  }
  else if(config->registerFormat == RegisterFormat::ScaledInt){
    *val = raw/ 1000.0f;
  }
  else{
    Serial.println("[ModbusRTU Reader] Unknown register format");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;

}

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


void ModbusRtuReader::build_request(uint8_t *buffer, uint16_t data_type_address){
  buffer[0] = config->slaveAddress;
  buffer[1] = config->functionCode;
  buffer[2] = (data_type_address & 0xFF00) >> 8;
  buffer[3] = data_type_address & 0x00FF;
  buffer[4] = 0x00;
  buffer[5] = 0x02;
  uint16_t request_crc = modbus_crc(buffer,6);
  buffer[6] = request_crc & 0x00FF;
  buffer[7] = (request_crc & 0xFF00) >> 8;        
}


int ModbusRtuReader::read_response(uint8_t *buffer){
  uint32_t startTime = millis();
  unsigned long lastByteReceived = micros();
  size_t index = 0;
  int capture;

  // Read response
  while(millis() - startTime < TIMEOUT){
    capture = module->readByte();
    if(capture != -1){
      if(index < RESPONSE_LEN){
	buffer[index] = capture;
      }
      index++;
      lastByteReceived = micros();
      continue;
    }
    // Nothing on the line. t3.5 of silence after a byte closes the frame
    if(index > 0 && (micros() - lastByteReceived) >= t35_us){
      break;
    }
  }
  last_rx_us = lastByteReceived;

  // Error checking
  if(index < MIN_FRAME_LEN){
    Serial.println("[ModbusRTU Reader] Response incomplete");
    return EXIT_FAILURE;
  }

  if(index > RESPONSE_LEN){
    Serial.println("[ModbusRTU Reader] Response longer than expected");
    return EXIT_FAILURE;
  }

  if(buffer[0] != config->slaveAddress){
    Serial.println("[ModbusRTU Reader] Slave address response does not match");
    return EXIT_FAILURE;
  }

  uint16_t response_crc = buffer[index-2] | buffer[index-1] << 8;
  if(modbus_crc(buffer,index-2) != response_crc){
    Serial.println("[ModbusRTU Reader] CRC does not match possible corruption in line");
    return EXIT_FAILURE;
  }

  if(buffer[1] & 0x80){
    request_exception(buffer[2]);
    return EXIT_FAILURE;
  }

  if(index != RESPONSE_LEN){
    Serial.println("[ModbusRTU Reader] Response incomplete");
    return EXIT_FAILURE;
  }
  if(buffer[1] != config->functionCode){
    Serial.println("[ModbusRTU Reader] Function code response does not match");
    return EXIT_FAILURE;
  }

  uint8_t bytes = buffer[2];
  if(bytes != 4){
    Serial.println("[ModbusRTU Reader] Expected byte length mismatch");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}


void ModbusRtuReader::request_exception(uint8_t exception){
  switch(exception){
  case 0x01:
    Serial.println("[ModbusRTU Reader] Illegal function passed");
    break;
  case 0x02:
    Serial.println("[ModbusRTU Reader] Illegal Data Address");
    break;
  case 0x03:
    Serial.println("[ModbusRTU Reader] Illegal data value");
    break;
  case 0x04:
    Serial.println("[ModbusRTU Reader] Slave device failure");
    break;
  case 0x05:
    Serial.println("[ModbusRTU Reader] Slave needed more time to respond");
    break;
  case 0x06:
    Serial.println("[ModbusRTU Reader] Slave busy");
    break;
  case 0x07:
    Serial.println("[ModbusRTU Reader] Negative acknowledge");
    break;
  case 0x08:
    Serial.println("[ModbusRTU Reader] Memory Parity Error");
    break;
  case 0x0A:
    Serial.println("[ModbusRTU Reader] Gateway Path unavailable");
    break;
  case 0x0B:
    Serial.println("[ModbusRTU Reader] Gateway target device failed to respond");
    break;
  default:
    Serial.println("[ModbusRTU Reader] Response error could not be identified");
    break;
  }
}
