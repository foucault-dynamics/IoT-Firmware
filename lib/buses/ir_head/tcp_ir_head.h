/**
 * @file
 * TcpIrHead, an IrHead carrying IEC 62056-21 bytes over TCP for simulator testing.
 */

#ifndef TCP_IR_HEAD_H
#define TCP_IR_HEAD_H

#include "ir_head.h"
#include "rs485_config.h"
#include "tcp_bus.h"

/**
 * Carries the optical port's IEC 62056-21 bytes over WiFi to
 * IrSim/IrSimTCP.py on a laptop, the same way TcpBus carries Modbus RTU to
 * ModbusSim/ModbusSimTCP.py.
 *
 * This lets Iec6205621Reader be tested against a meter that answers like a real
 * one, with no extra hardware on the desk. Testing only.
 */
class TcpIrHead : public IrHead {
 private:
  TcpBus bus;  ///< Socket to the simulator. Reconnects on every send().

 public:
  /**
   * Stores where the simulator is. Nothing connects until init().
   *
   * @param[in] config  Simulator host, port and connect timeout.
   */
  explicit TcpIrHead(TcpBusConfig config);

  int init() override;
  int send(const uint8_t *data, size_t len) override;
  int readByte() override;
  bool available() override;

  /**
   * Logs the switch, since there is no baud rate on a socket.
   *
   * @param[in] baud  Rate the reader negotiated, in baud.
   */
  void setBaudRate(uint32_t baud) override;
};

#endif
