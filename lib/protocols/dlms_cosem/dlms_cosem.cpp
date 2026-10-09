/**
 * @file
 * DlmsCosemReader implementation.
 */

#include "dlms_cosem.h"
#include <Arduino.h>
#include <cstdlib>

DlmsCosemReader::DlmsCosemReader(const DlmsCosemConfig &config): config(config){
}

int DlmsCosemReader::init(Module &module){
  this->module = &module;

  if(hdlcAddress(config.clientSap, 0, 1, &client) != EXIT_SUCCESS){
    Serial.println("[DlmsCosemReader] client SAP does not fit in one byte");
    return EXIT_FAILURE;
  }
  if(hdlcAddress(config.serverLogical, config.serverPhysical,
                 config.serverAddrLen, &server) != EXIT_SUCCESS){
    Serial.println("[DlmsCosemReader] invalid server address or size");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int DlmsCosemReader::get_import(float *val){
  return EXIT_FAILURE;
}

int DlmsCosemReader::get_export(float *val){
  return EXIT_FAILURE;
}

int DlmsCosemReader::get_voltage(float *val){
  return EXIT_FAILURE;
}

int DlmsCosemReader::readFrame(uint8_t *buf, size_t *len){
  uint32_t startTime = millis();
  size_t index = 0;
  size_t total = 0;

  while(millis() - startTime < DLMS_TIMEOUT){
    int capture = module->readByte();
    if(capture == -1){
      continue;
    }
    uint8_t b = capture;
    if(index == 0){
      if(b == HDLC_FLAG){
        buf[index++] = b;
      }
      continue;
    }
    if(index == 1){
      if(b == HDLC_FLAG){
        continue;
      }
      if((b & 0xF0) != (HDLC_FORMAT_TYPE_A & 0xF0)){
        index = 0;
        continue;
      }
    }
    buf[index++] = b;
    if(index == 3){
      size_t length = ((buf[1] & 0x07) << 8) | buf[2];
      if(length < 7 || length + 2 > HDLC_FRAME_MAX){
        index = 0;
        continue;
      }
      total = length + 2;
    }
    if(index == total){
      *len = total;
      return EXIT_SUCCESS;
    }
  }
  Serial.println("[DlmsCosemReader] no complete frame before timeout");
  return EXIT_FAILURE;
}
