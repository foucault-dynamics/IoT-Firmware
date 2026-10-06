// Just enough of the Arduino core to build Iec6205621Reader and
// SimulatedIrHead on a laptop, for IrSim/native/run.sh. Not firmware.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include "WString.h"
unsigned long millis();
struct FakeSerialOut {
  void println(const char *s) { std::printf("    %s\n", s); }
  void printf(const char *fmt, ...) { va_list a; va_start(a, fmt); std::printf("    "); std::vprintf(fmt, a); va_end(a); }
};
extern FakeSerialOut Serial;
