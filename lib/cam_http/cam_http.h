#ifndef CAM_HTTP_H
#define CAM_HTTP_H

#include <Arduino.h>

#include "module.h"
#include "node_config.h"
#include "reader.h"

/*
 * AI-on-the-edge-device HTTP reader.
 *
 * Asks the cam's "/json" API for the configured flow and parses the
 * "value" it reports. Sits on top of any Module that can carry the
 * request and hand the response body back (lib/http_bus today, the wired
 * lib/esp32cam link later).
 *
 * The cam only recognises digits, so it reports a single register: the
 * meter's import reading. get_export()/get_voltage() have nothing to
 * answer with.
 */
class CamHttpReader : public Reader {
 public:
  CamHttpReader(const CamHttpConfig &config);
  int init(Module &module) override;
  int get_import(float *val) override;
  int get_export(float *val) override;
  int get_voltage(float *val) override;

 private:
  CamHttpConfig config;
  // Requests the configured flow and parses its value into *val.
  int read_flow(const char *flow, float *val);
  // Drains the module's response bytes into out.
  int read_response(String &out);
};

#endif
