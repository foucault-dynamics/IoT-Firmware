#ifndef IR_HEAD_H
#define IR_HEAD_H

#include "module.h"
#include "pin_config.h"

/*
 * Placeholder: IR optical probe on the meter's front panel.
 *
 * Many meters expose the same Modbus register map over the optical port
 * as over RS485, so once this is implemented ModbusRtuReader can run on
 * it unchanged:
 *
 *   IrHead head(irPins, 9600);
 *   ModbusRtuReader reader(head);
 *   ModbusMeter meter(reader, 1, registerMap);
 *
 * TODO: decide the modulation scheme and fill in the class.
 */

#endif
