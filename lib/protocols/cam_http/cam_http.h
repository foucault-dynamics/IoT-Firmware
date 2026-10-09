/**
 * @file
 * CamHttpReader, the Reader for the ESP32-CAM running AI-on-the-edge-device.
 */

#ifndef CAM_HTTP_H
#define CAM_HTTP_H

#include <Arduino.h>

#include "module.h"
#include "cv_config.h"
#include "reader.h"

/**
 * Reads the meter's digits from an AI-on-the-edge-device camera.
 *
 * Asks the cam's "/json" API for the configured flow and parses the "value"
 * it reports. Sits on top of any Module that can carry the request and hand
 * the response body back, HttpBus today and a wired UART link later.
 *
 * The cam only recognises digits, so it reports a single register: the
 * meter's import reading. get_export() and get_voltage() have nothing to
 * answer with and always fail.
 */
class CamHttpReader : public Reader {
 public:
  /**
   * Stores the config. Nothing is sent until a getter is called.
   *
   * @param[in] config  Cam address, API path and flow name, copied.
   */
  CamHttpReader(const CamHttpConfig &config);

  /**
   * Attaches the bus and checks the config names a host and a flow.
   *
   * @param[in] module  Bus that carries the HTTP request, usually HttpBus.
   * @retval EXIT_SUCCESS  Ready to read.
   * @retval EXIT_FAILURE  The host or flow name is empty.
   */
  int init(Module &module) override;

  /**
   * Requests the configured flow and parses its value.
   *
   * @param[out] val  The digits the cam read, as kWh.
   * @retval EXIT_SUCCESS  @p val holds a fresh reading.
   * @retval EXIT_FAILURE  The request failed, the JSON was bad, or the cam
   *                       reported an error or no value yet.
   */
  int get_import(float *val) override;

  /**
   * Always fails, because the cam only reads the import register.
   *
   * @param[out] val  Untouched.
   * @retval EXIT_FAILURE  Always.
   */
  int get_export(float *val) override;

  /**
   * Always fails, because the cam cannot read voltage.
   *
   * @param[out] val  Untouched.
   * @retval EXIT_FAILURE  Always.
   */
  int get_voltage(float *val) override;

 private:
  CamHttpConfig config;  ///< Cam address, API path and flow name.

  /**
   * Requests the cam's JSON and parses one flow's value.
   *
   * A flow counts as good when its "error" field is empty or "no error".
   *
   * @param[in]  flow  Flow name as it appears in the cam's JSON.
   * @param[out] val   Parsed value. Untouched on failure.
   * @retval EXIT_SUCCESS  @p val holds the flow's value.
   * @retval EXIT_FAILURE  Request, parse, or cam side error.
   */
  int read_flow(const char *flow, float *val);

  /**
   * Drains the module's buffered response body into a string.
   *
   * @param[out] out  Response body. Cleared first.
   * @retval EXIT_SUCCESS  Got a non empty body.
   * @retval EXIT_FAILURE  The body was empty.
   */
  int read_response(String &out);
};

#endif
