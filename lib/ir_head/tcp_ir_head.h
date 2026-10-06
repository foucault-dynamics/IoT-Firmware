#ifndef TCP_IR_HEAD_H
#define TCP_IR_HEAD_H

#include "ir_head.h"
#include "node_config.h"
#include "tcp_bus.h"

/*
 * Testing only: carries the optical port's IEC 62056-21 bytes over WiFi to
 * IrSim/IrSimTCP.py running on a laptop, the same way TcpBus carries Modbus
 * RTU to ModbusSim/ModbusSimTCP.py. Lets Iec6205621Reader be tested against
 * a meter that answers like a real one, with no extra hardware on the desk.
 *
 * There's no baud rate on a socket, so setBaudRate() only logs.
 */
class TcpIrHead : public IrHead {
 private:
  TcpBus bus;

 public:
  explicit TcpIrHead(TcpBusConfig config);

  int init() override;
  int send(const uint8_t *data, size_t len) override;
  int readByte() override;
  bool available() override;
  void setBaudRate(uint32_t baud) override;
};

#endif
