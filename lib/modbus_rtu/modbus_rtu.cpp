#include "modbus_rtu.h"

// TODO: build request frames, CRC16, parse responses into Payload.
//
// Request (read holding registers), 8 bytes:
//   [slaveAddress][0x03][regHi][regLo][countHi][countLo][crcLo][crcHi]
//
// Response:
//   [slaveAddress][0x03][byteCount][data...][crcLo][crcHi]
//   Function code with bit 7 set (0x83) means an exception response.
//
// poll() = build request, stream.send(), wait for the reply,
//          stream.receive(), verify address + CRC, decode registers
//          into out.kwh_import / out.kwh_export / out.voltage.
//
// NOTE: Modbus RTU delimits frames by 3.5 character times of silence,
// which Module::receive() does not model. Needs a read timeout policy
// here: keep reading until the expected byte count arrives or a
// deadline passes, then validate CRC.
