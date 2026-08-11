#include "module.h"
#include "PinConfig.h"

#ifndef RS485_H
#define RS485_H

enum RS485_protocol{
  MODBUS_RTU,
  EDMI,
  DLMS_COSEM
}

class RS485 : public Module{

 private:
  UARTPinConfig pinConfig;
  RS485_protocol protocol;
  
 public:
  RS485();
  int receive();
  int send();    
}

#endif
