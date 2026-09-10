#include "cam_http.h"

#include <ArduinoJson.h>

#include <cstdlib>
#include <cstring>

// Response bodies from the cam are a few hundred bytes; reserve up front so
// draining byte by byte does not realloc on every append.
#define RESPONSE_RESERVE 512

int CamHttpReader::init(Module &module, const void *config) {
  this->module = &module;
  this->config = static_cast<const CamHttpConfig *>(config);

  if (this->config->host == nullptr || this->config->flowName == nullptr) {
    Serial.println("[Cam HTTP Reader] Host or flow name missing from config");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int CamHttpReader::get_import(float *val) {
  return read_flow(config->flowName, val);
}

int CamHttpReader::get_export(float *val) {
  (void)val;
  return EXIT_FAILURE;
}

int CamHttpReader::get_voltage(float *val) {
  (void)val;
  return EXIT_FAILURE;
}

int CamHttpReader::read_flow(const char *flow, float *val) {
  // Send request
  String url = String("http://") + config->host + config->path;
  if (module->send(reinterpret_cast<const uint8_t *>(url.c_str()), url.length()) == EXIT_FAILURE) {
    return EXIT_FAILURE;
  }

  // Capture response
  String body;
  if (read_response(body) == EXIT_FAILURE) {
    return EXIT_FAILURE;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[Cam HTTP Reader] JSON parse failed: %s\n", err.c_str());
    return EXIT_FAILURE;
  }

  JsonVariant number = doc[flow];
  if (number.isNull()) {
    Serial.printf("[Cam HTTP Reader] Flow \"%s\" not present in response.\n", flow);
    return EXIT_FAILURE;
  }

  // The cam sets this field to the literal string "no error" on a good read,
  // so an empty field and that sentinel both mean the reading is usable.
  const char *errorMessage = number["error"] | "";
  if (errorMessage[0] != '\0' && strcmp(errorMessage, "no error") != 0) {
    Serial.printf("[Cam HTTP Reader] Cam reported an error for \"%s\": %s\n", flow, errorMessage);
    return EXIT_FAILURE;
  }

  const char *valueStr = number["value"] | "";
  if (valueStr[0] == '\0') {
    Serial.printf("[Cam HTTP Reader] Flow \"%s\" has no value yet.\n", flow);
    return EXIT_FAILURE;
  }

  *val = atof(valueStr);
  return EXIT_SUCCESS;
}

int CamHttpReader::read_response(String &out) {
  out = String();
  out.reserve(RESPONSE_RESERVE);

  while (module->available()) {
    int capture = module->readByte();
    if (capture < 0) {
      break;
    }
    out += static_cast<char>(capture);
  }

  if (out.length() == 0) {
    Serial.println("[Cam HTTP Reader] Empty response body");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
