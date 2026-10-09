#include "hdlc.h"
#include <cstddef>
#include <cstdlib>
#include <cstring>

uint16_t hdlcFcs(const uint8_t *data, size_t len){
  uint16_t crc = 0xffff;
  for(size_t i = 0; i < len; i++){
    crc ^= data[i];
    for(unsigned k = 0; k < 8; k++){
      crc = (crc & 1) != 0 ? (crc >> 1) ^ 0x8408 : crc >> 1; 
    }
  }
  return ~crc;
}

int hdlcAddress(uint16_t upper, uint16_t lower, uint8_t size, HdlcAddress *out){
  uint16_t max = size == 4 ? 0x3FFF : 0x7F;
  if(upper > max || (size > 1 && lower > max)){
    return EXIT_FAILURE;
  }
  switch(size){
  case 1:
    out->bytes[0] = upper << 1;
    break;
  case 2:
    out->bytes[0] = upper << 1;
    out->bytes[1] = lower << 1;
    break;
  case 4:
    out->bytes[0] = (upper >> 7) << 1;
    out->bytes[1] = (upper & 0x7F) << 1;
    out->bytes[2] = (lower >> 7) << 1;
    out->bytes[3] = (lower & 0x7F) << 1;
    break;
  default:
    return EXIT_FAILURE;
  }
  out->bytes[size - 1] |= 1;
  out->len = size;
  return EXIT_SUCCESS;
}

int hdlcBuildFrame(const HdlcAddress *dest, const HdlcAddress *src,
                   uint8_t control, const uint8_t *info, size_t infoLen,
                   uint8_t *out, size_t *outLen){
  if(infoLen > HDLC_INFO_MAX){
    return EXIT_FAILURE;
  }
  size_t length = 2 + dest->len + src->len + 1 + 2;
  if(infoLen > 0){
    length += 2 + infoLen;
  }
  size_t pos = 0;
  out[pos++] = HDLC_FLAG;
  out[pos++] = HDLC_FORMAT_TYPE_A | (length >> 8);
  out[pos++] = length & 0xFF;
  memcpy(out + pos, dest->bytes, dest->len);
  pos += dest->len;
  memcpy(out + pos, src->bytes, src->len);
  pos += src->len;
  out[pos++] = control;
  if(infoLen > 0){
    uint16_t hcs = hdlcFcs(out + 1, pos - 1);
    out[pos++] = hcs & 0xFF;
    out[pos++] = hcs >> 8;
    memcpy(out + pos, info, infoLen);
    pos += infoLen;
  }
  uint16_t fcs = hdlcFcs(out + 1, pos - 1);
  out[pos++] = fcs & 0xFF;
  out[pos++] = fcs >> 8;
  out[pos++] = HDLC_FLAG;
  *outLen = pos;
  return EXIT_SUCCESS;
}

namespace {

int parseAddress(const uint8_t *buf, size_t end, size_t *pos, HdlcAddress *out){
  size_t n = 0;
  while(true){
    if(*pos >= end || n == HDLC_ADDRESS_MAX){
      return EXIT_FAILURE;
    }
    uint8_t b = buf[(*pos)++];
    out->bytes[n++] = b;
    if((b & 1) != 0){
      break;
    }
  }
  if(n == 3){
    return EXIT_FAILURE;
  }
  out->len = n;
  return EXIT_SUCCESS;
}

}  // namespace

int hdlcParseFrame(const uint8_t *buf, size_t len, HdlcFrame *out){
  if(len < 9 || buf[0] != HDLC_FLAG || buf[len - 1] != HDLC_FLAG){
    return EXIT_FAILURE;
  }
  if((buf[1] & 0xF8) != HDLC_FORMAT_TYPE_A){
    return EXIT_FAILURE;
  }
  size_t length = ((buf[1] & 0x07) << 8) | buf[2];
  if(length != len - 2){
    return EXIT_FAILURE;
  }
  size_t fcsPos = len - 3;
  size_t pos = 3;
  if(parseAddress(buf, fcsPos, &pos, &out->dest) != EXIT_SUCCESS ||
     parseAddress(buf, fcsPos, &pos, &out->src) != EXIT_SUCCESS ||
     pos >= fcsPos){
    return EXIT_FAILURE;
  }
  out->control = buf[pos++];
  size_t rest = fcsPos - pos;
  if(rest == 0){
    out->info = nullptr;
    out->infoLen = 0;
  } else if(rest >= 3){
    uint16_t hcs = hdlcFcs(buf + 1, pos - 1);
    if(buf[pos] != (hcs & 0xFF) || buf[pos + 1] != (hcs >> 8)){
      return EXIT_FAILURE;
    }
    out->info = buf + pos + 2;
    out->infoLen = rest - 2;
  } else {
    return EXIT_FAILURE;
  }
  uint16_t fcs = hdlcFcs(buf + 1, fcsPos - 1);
  if(buf[fcsPos] != (fcs & 0xFF) || buf[fcsPos + 1] != (fcs >> 8)){
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
