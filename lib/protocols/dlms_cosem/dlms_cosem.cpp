/**
 * @file
 * DlmsCosemReader implementation.
 */

#include "dlms_cosem.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>

namespace {

/** LLC header in front of every APDU this client sends. */
const uint8_t LLC_SEND[DLMS_LLC_LEN] = {0xE6, 0xE6, 0x00};
/** LLC header in front of every APDU the meter sends back. */
const uint8_t LLC_RECV[DLMS_LLC_LEN] = {0xE6, 0xE7, 0x00};

/** AARQ APDU, built once since nothing in it changes between sessions. */
const uint8_t AARQ[] = {
  0x60, 0x1D,
  0xA1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08, 0x01, 0x01,
  0xBE, 0x10, 0x04, 0x0E,
  0x01, 0x00, 0x00, 0x00, 0x06,
  0x5F, 0x1F, 0x04, 0x00, 0x00, 0x00, 0x10,
  DLMS_APDU_MAX >> 8, DLMS_APDU_MAX & 0xFF,
};

/**
 * Reads a BER length in short form, or long form with one length byte.
 *
 * @param[in]     buf  Buffer being walked.
 * @param[in]     end  Index just past the last byte that may be read.
 * @param[in,out] pos  Index of the length, moved past it.
 * @param[out]    len  Length read.
 * @retval EXIT_SUCCESS  @p len holds the length.
 * @retval EXIT_FAILURE  The length runs past @p end, or needs more than one
 *                       length byte.
 */
int readBerLength(const uint8_t *buf, size_t end, size_t *pos, size_t *len){
  if(*pos >= end){
    return EXIT_FAILURE;
  }
  uint8_t first = buf[(*pos)++];
  if(first < 0x80){
    *len = first;
    return EXIT_SUCCESS;
  }
  if(first == 0x81 && *pos < end){
    *len = buf[(*pos)++];
    return EXIT_SUCCESS;
  }
  return EXIT_FAILURE;
}

/**
 * Logs why the meter could not serve a GET, like a Modbus exception.
 *
 * @param[in] result  Data-access-result code from the response.
 */
void logAccessResult(uint8_t result){
  switch(result){
  case 1:
    Serial.println("[DlmsCosemReader] GET failed: hardware fault");
    break;
  case 2:
    Serial.println("[DlmsCosemReader] GET failed: temporary failure");
    break;
  case 3:
    Serial.println("[DlmsCosemReader] GET failed: read denied");
    break;
  case 4:
    Serial.println("[DlmsCosemReader] GET failed: object undefined");
    break;
  case 9:
    Serial.println("[DlmsCosemReader] GET failed: object class inconsistent");
    break;
  case 11:
    Serial.println("[DlmsCosemReader] GET failed: object unavailable");
    break;
  case 12:
    Serial.println("[DlmsCosemReader] GET failed: type unmatched");
    break;
  case 13:
    Serial.println("[DlmsCosemReader] GET failed: scope of access violated");
    break;
  case 250:
    Serial.println("[DlmsCosemReader] GET failed: other reason");
    break;
  default:
    Serial.printf("[DlmsCosemReader] GET failed: data access result %u\n", result);
    break;
  }
}

/**
 * Multiplies a value by 10 to the power of a scaler. Divides for a negative
 * scaler, since 10^n is exact in a double but 10^-n is not.
 *
 * @param[in] value   Raw register value.
 * @param[in] scaler  Power of ten from the register's scaler_unit.
 * @return The scaled value.
 */
double applyScaler(double value, int8_t scaler){
  double power = 1;
  for(int i = 0; i < abs(scaler); i++){
    power *= 10;
  }
  return scaler < 0 ? value / power : value * power;
}

/**
 * Compares two encoded HDLC addresses.
 *
 * @param[in] a  First address.
 * @param[in] b  Second address.
 * @return True when both have the same length and bytes.
 */
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
  double wh = 0;
  if(readRegister(config.importObis, DLMS_UNIT_WH, &wh) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  *val = wh / 1000;
  return EXIT_SUCCESS;
}

int DlmsCosemReader::get_export(float *val){
  double wh = 0;
  if(readRegister(config.exportObis, DLMS_UNIT_WH, &wh) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  *val = wh / 1000;
  return EXIT_SUCCESS;
}

int DlmsCosemReader::get_voltage(float *val){
  double volts = 0;
  if(readRegister(config.voltageObis, DLMS_UNIT_V, &volts) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  *val = volts;
  return EXIT_SUCCESS;
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

int DlmsCosemReader::exchange(const uint8_t *apdu, size_t apduLen, uint8_t *resp, size_t *respLen){
  if(apduLen > DLMS_APDU_MAX){
    Serial.println("[DlmsCosemReader] APDU too long for one frame");
    return EXIT_FAILURE;
  }
  uint8_t info[HDLC_INFO_MAX];
  memcpy(info, LLC_SEND, DLMS_LLC_LEN);
  memcpy(info + DLMS_LLC_LEN, apdu, apduLen);
  uint8_t control = (vr << 5) | HDLC_PF | (vs << 1);
  if(sendFrame(control, info, DLMS_LLC_LEN + apduLen) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }

  uint8_t buf[HDLC_FRAME_MAX];
  HdlcFrame frame;
  if(receive(buf, &frame) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  if((frame.control & 1) != 0){
    Serial.println("[DlmsCosemReader] expected an I-frame");
    return EXIT_FAILURE;
  }
  if((frame.control & HDLC_PF) == 0){
    Serial.println("[DlmsCosemReader] segmented reply not supported");
    return EXIT_FAILURE;
  }
  uint8_t ns = (frame.control >> 1) & 0x07;
  uint8_t nr = frame.control >> 5;
  if(ns != vr || nr != ((vs + 1) & 0x07)){
    Serial.println("[DlmsCosemReader] reply out of sequence");
    return EXIT_FAILURE;
  }
  if(frame.infoLen < DLMS_LLC_LEN || frame.infoLen > HDLC_INFO_MAX ||
     memcmp(frame.info, LLC_RECV, DLMS_LLC_LEN) != 0){
    Serial.println("[DlmsCosemReader] reply is not a DLMS response");
    return EXIT_FAILURE;
  }

  *respLen = frame.infoLen - DLMS_LLC_LEN;
  memcpy(resp, frame.info + DLMS_LLC_LEN, *respLen);
  vs = (vs + 1) & 0x07;
  vr = (vr + 1) & 0x07;
  return EXIT_SUCCESS;
}

size_t DlmsCosemReader::buildAarq(uint8_t *out){
  memcpy(out, AARQ, sizeof(AARQ));
  return sizeof(AARQ);
}

int DlmsCosemReader::checkAare(const uint8_t *apdu, size_t len){
  size_t pos = 0;
  size_t bodyLen = 0;
  if(len < 2 || apdu[pos++] != 0x61 ||
     readBerLength(apdu, len, &pos, &bodyLen) != EXIT_SUCCESS || bodyLen > len - pos){
    Serial.println("[DlmsCosemReader] reply is not a valid AARE");
    return EXIT_FAILURE;
  }

  size_t end = pos + bodyLen;
  int result = -1;
  int diagnostic = -1;
  bool initiated = false;
  while(pos < end){
    uint8_t tag = apdu[pos++];
    size_t valueLen = 0;
    if(readBerLength(apdu, end, &pos, &valueLen) != EXIT_SUCCESS || valueLen > end - pos){
      Serial.println("[DlmsCosemReader] AARE field runs past its end");
      return EXIT_FAILURE;
    }
    const uint8_t *value = apdu + pos;
    switch(tag){
    case 0xA2:
      if(valueLen == 3 && value[0] == 0x02 && value[1] == 0x01){
        result = value[2];
      }
      break;
    case 0xA3:
      if(valueLen == 5 && value[2] == 0x02 && value[3] == 0x01){
        diagnostic = value[4];
      }
      break;
    case 0xBE:
      if(valueLen >= 3 && value[0] == 0x04){
        initiated = value[2] == 0x08;
      }
      break;
    }
    pos += valueLen;
  }

  if(result != 0){
    Serial.printf("[DlmsCosemReader] association rejected, result %d, diagnostic %d\n",
                  result, diagnostic);
    return EXIT_FAILURE;
  }
  if(!initiated){
    Serial.println("[DlmsCosemReader] meter accepted the association but not the xDLMS initiate");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

void DlmsCosemReader::buildGetRequest(const uint8_t *obis, uint8_t attribute, uint8_t *out){
  out[0] = 0xC0;
  out[1] = 0x01;
  out[2] = 0xC1;
  out[3] = DLMS_CLASS_REGISTER >> 8;
  out[4] = DLMS_CLASS_REGISTER & 0xFF;
  memcpy(out + 5, obis, 6);
  out[11] = attribute;
  out[12] = 0x00;
}

int DlmsCosemReader::parseGetResponse(const uint8_t *apdu, size_t len, const uint8_t **data, size_t *dataLen){
  if(len < DLMS_GET_RESPONSE_HEADER_LEN || apdu[0] != 0xC4){
    Serial.println("[DlmsCosemReader] reply is not a GET response");
    return EXIT_FAILURE;
  }
  if(apdu[1] == 0x02){
    Serial.println("[DlmsCosemReader] block transfer not supported");
    return EXIT_FAILURE;
  }
  if(apdu[1] != 0x01 || apdu[2] != 0xC1){
    Serial.println("[DlmsCosemReader] unexpected GET response type or invoke id");
    return EXIT_FAILURE;
  }
  if(apdu[3] == 0x01 && len > DLMS_GET_RESPONSE_HEADER_LEN){
    logAccessResult(apdu[4]);
    return EXIT_FAILURE;
  }
  if(apdu[3] != 0x00 || len == DLMS_GET_RESPONSE_HEADER_LEN){
    Serial.println("[DlmsCosemReader] GET response has no Data");
    return EXIT_FAILURE;
  }
  *data = apdu + DLMS_GET_RESPONSE_HEADER_LEN;
  *dataLen = len - DLMS_GET_RESPONSE_HEADER_LEN;
  return EXIT_SUCCESS;
}

int DlmsCosemReader::decodeNumber(const uint8_t *data, size_t len, double *val){
  if(len == 0){
    Serial.println("[DlmsCosemReader] empty Data");
    return EXIT_FAILURE;
  }
  size_t size = 0;
  switch(data[0]){
  case 0x0F:
  case 0x11:
    size = 1;
    break;
  case 0x10:
  case 0x12:
    size = 2;
    break;
  case 0x05:
  case 0x06:
  case 0x17:
    size = 4;
    break;
  case 0x15:
    size = 8;
    break;
  default:
    Serial.printf("[DlmsCosemReader] Data type 0x%02X is not a number\n", data[0]);
    return EXIT_FAILURE;
  }
  if(len < 1 + size){
    Serial.println("[DlmsCosemReader] Data cut short");
    return EXIT_FAILURE;
  }

  uint64_t raw = 0;
  for(size_t i = 1; i <= size; i++){
    raw = (raw << 8) | data[i];
  }
  switch(data[0]){
  case 0x0F:
    *val = (int8_t)raw;
    break;
  case 0x10:
    *val = (int16_t)raw;
    break;
  case 0x05:
    *val = (int32_t)raw;
    break;
  case 0x17: {
    uint32_t bits = raw;
    float f;
    memcpy(&f, &bits, sizeof(f));
    *val = f;
    break;
  }
  default:
    *val = raw;
    break;
  }
  return EXIT_SUCCESS;
}

int DlmsCosemReader::decodeScalerUnit(const uint8_t *data, size_t len, uint8_t unit, int8_t *scaler){
  if(len != 6 || data[0] != 0x02 || data[1] != 0x02 || data[2] != 0x0F || data[4] != 0x16){
    Serial.println("[DlmsCosemReader] Data is not a scaler_unit");
    return EXIT_FAILURE;
  }
  if(data[5] != unit){
    Serial.printf("[DlmsCosemReader] register unit %u, expected %u\n", data[5], unit);
    return EXIT_FAILURE;
  }
  *scaler = (int8_t)data[3];
  return EXIT_SUCCESS;
}

int DlmsCosemReader::associate(){
  uint8_t aarq[DLMS_APDU_MAX];
  size_t aarqLen = buildAarq(aarq);
  uint8_t resp[DLMS_APDU_MAX];
  size_t respLen = 0;
  if(exchange(aarq, aarqLen, resp, &respLen) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  return checkAare(resp, respLen);
}

int DlmsCosemReader::getAttribute(const uint8_t *obis, uint8_t attribute, uint8_t *resp,
                                  const uint8_t **data, size_t *dataLen){
  uint8_t request[DLMS_GET_REQUEST_LEN];
  buildGetRequest(obis, attribute, request);
  size_t respLen = 0;
  if(exchange(request, sizeof(request), resp, &respLen) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  return parseGetResponse(resp, respLen, data, dataLen);
}

int DlmsCosemReader::readAssociated(const uint8_t *obis, uint8_t unit, double *val){
  if(associate() != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  uint8_t resp[DLMS_APDU_MAX];
  const uint8_t *data = nullptr;
  size_t dataLen = 0;
  int8_t scaler = 0;
  if(getAttribute(obis, DLMS_ATTR_SCALER_UNIT, resp, &data, &dataLen) != EXIT_SUCCESS ||
     decodeScalerUnit(data, dataLen, unit, &scaler) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  double raw = 0;
  if(getAttribute(obis, DLMS_ATTR_VALUE, resp, &data, &dataLen) != EXIT_SUCCESS ||
     decodeNumber(data, dataLen, &raw) != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  *val = applyScaler(raw, scaler);
  return EXIT_SUCCESS;
}

int DlmsCosemReader::readRegister(const uint8_t *obis, uint8_t unit, double *val){
  if(connect() != EXIT_SUCCESS){
    return EXIT_FAILURE;
  }
  int result = readAssociated(obis, unit, val);
  disconnect();
  return result;
}
