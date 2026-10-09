// Runs the real Iec6205621Reader + SimulatedIrHead against fakes, on the laptop.
#include "Arduino.h"
#include "iec62056_21.h"
#include "simulated_ir_head.h"
#include <deque>
#include <cstring>
FakeSerialOut Serial;
static unsigned long now = 0;
unsigned long millis() { return now++; }   // time moves 1 ms per call

// A meter like IrSim/ir_meter.py on a real UART: answers the request only at
// 300 baud, sends a framed block (STX..ETX BCC) at the negotiated baud, then
// goes back to 300.
class UartMeter : public IrHead {
  std::deque<uint8_t> rx;
  uint32_t baud = 300;
  bool awaitingAck = false;
  float imp = 1234.567f, exp_ = 45.123f;
 public:
  bool echo = false;   // IR head sees its own LED
  int init() override { return EXIT_SUCCESS; }
  void setBaudRate(uint32_t b) override { baud = b; }
  int readByte() override { if (rx.empty()) return -1; int b = rx.front(); rx.pop_front(); return b; }
  bool available() override { return !rx.empty(); }
  void push(const std::string &s) { for (unsigned char c : s) rx.push_back(c); }
  int send(const uint8_t *d, size_t n) override {
    std::string m((const char *)d, n);
    if (echo) push(m);
    if (baud != 300) { std::printf("    [meter] got %zu bytes at %u baud -- meter listens at 300, ignored\n", n, baud); return EXIT_SUCCESS; }
    if (m == "/?!\r\n") { push("/EMH5EM211\r\n"); awaitingAck = true; }
    else if (awaitingAck && m[0] == 0x06) {
      awaitingAck = false; imp += 0.1f; exp_ += 0.05f;
      char body[200];
      std::snprintf(body, sizeof body, "0-0:96.1.0(12345678)\r\n1-0:1.8.0(%010.3f*kWh)\r\n1-0:2.8.0(%010.3f*kWh)\r\n!\r\n\x03", imp, exp_);
      uint8_t bcc = 0; for (char *p = body; *p; p++) bcc ^= (uint8_t)*p;
      push(std::string("\x02") + body + std::string(1, (char)bcc));
      // meter drops back to 300 on its own; the reader's UART is still at 9600 here
    }
    return EXIT_SUCCESS;
  }
};

static int run(const char *name, IrHead &head, int polls) {
  std::printf("== %s\n", name);
  Iec62056Config cfg{}; cfg.bus.baudRate = 300;
  Iec6205621Reader reader(cfg);
  reader.init(head);
  int ok = 0;
  for (int i = 1; i <= polls; i++) {
    float imp = 0, ex = 0;
    if (reader.get_import(&imp) == EXIT_SUCCESS && reader.get_export(&ex) == EXIT_SUCCESS) {
      std::printf("  poll %d: import=%.3f export=%.3f\n", i, imp, ex); ok++;
    } else std::printf("  poll %d: FAILED\n", i);
  }
  std::printf("  %d/%d ok\n\n", ok, polls);
  return ok == polls ? 0 : 1;
}

int main() {
  int fails = 0;
  SimulatedIrHead sim; fails += run("SimulatedIrHead", sim, 3);
  UartMeter uart; fails += run("Real-UART-like meter (framed, baud drops to 300)", uart, 3);
  UartMeter echoing; echoing.echo = true; fails += run("Same, with IR echo", echoing, 3);
  return fails;
}
