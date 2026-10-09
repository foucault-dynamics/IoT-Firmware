/**
 * @file
 * DlmsCosemReader implementation.
 */

#include "dlms_cosem.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>

namespace {

bool sameAddress(const HdlcAddress &a, const HdlcAddress &b){
  return a.len == b.len && memcmp(a.bytes, b.bytes, a.len) == 0;
}

}  // namespace

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

int DlmsCosemReader::sendFrame(uint8_t control, const uint8_t *info, size_t infoLen){
  uint8_t buf[HDLC_FRAME_MAX];
  size_t len = 0;
  if(hdlcBuildFrame(&server, &client, control, info, infoLen, buf, &len) != EXIT_SUCCESS){
    Serial.println("[DlmsCosemReader] info field too long for a frame");
    return EXIT_FAILURE;
  }
  if(module->send(buf, len) != EXIT_SUCCESS){
    Serial.println("[DlmsCosemReader] error with bus");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int DlmsCosemReader::receive(uint8_t *buf, HdlcFrame *frame){
  size_t len = 0;
  if(readFrame(buf, &len) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  if(hdlcParseFrame(buf, len, frame) != EXIT_SUCCESS){
    Serial.println("[DlmsCosemReader] malformed frame or bad checksum");
    return EXIT_FAILURE;
  }
  if(!sameAddress(frame->dest, client) || !sameAddress(frame->src, server)){
    Serial.println("[DlmsCosemReader] frame not from our server to our client");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int DlmsCosemReader::connect(){
  if(sendFrame(HDLC_SNRM, nullptr, 0) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  uint8_t buf[HDLC_FRAME_MAX];
  HdlcFrame frame;
  if(receive(buf, &frame) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  if(frame.control == HDLC_DM){
    Serial.println("[DlmsCosemReader] meter refused the link (DM)");
    return EXIT_FAILURE;
  }
  if(frame.control != HDLC_UA){
    Serial.println("[DlmsCosemReader] unexpected reply to SNRM");
    return EXIT_FAILURE;
  }
  vs = 0;
  vr = 0;
  return EXIT_SUCCESS;
}

int DlmsCosemReader::disconnect(){
  if(sendFrame(HDLC_DISC, nullptr, 0) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  uint8_t buf[HDLC_FRAME_MAX];
  HdlcFrame frame;
  if(receive(buf, &frame) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  if(frame.control != HDLC_UA && frame.control != HDLC_DM){
    Serial.println("[DlmsCosemReader] unexpected reply to DISC");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
