#include <Arduino.h>
#include "nodes/nodes.h"

enum class ModuleType : uint8_t {
  Unknown = 0,
  Rs485Node = 1,
  IrNode = 2,
  CvNode = 3,
  Substation = 4,
  Gateway = 5,
};

// Change this to read the Module ID pin
static ModuleType readModuleType() {
  return ModuleType::CvNode;
}

static ModuleType moduleType = ModuleType::Unknown;

void setup() {
  Serial.begin(115200);

  moduleType = readModuleType();
  Serial.printf("[BOOT] module type %d\n", (int)moduleType);

  switch (moduleType) {
  case ModuleType::Rs485Node:
    rs485NodeSetup();
    break;
  case ModuleType::IrNode:
    irNodeSetup();
    break;
  case ModuleType::CvNode:
    cvNodeSetup();
    break;
  case ModuleType::Substation:
    substationSetup();
    break;
  case ModuleType::Gateway:
    gatewaySetup();
    break;
  default:
    Serial.println("[BOOT] Unknown module type. Idling.");
    break;
  }
}

void loop() {
  switch (moduleType) {
  case ModuleType::Rs485Node:
    rs485NodeLoop();
    break;
  case ModuleType::IrNode:
    irNodeLoop();
    break;
  case ModuleType::CvNode:
    cvNodeLoop();
    break;
  case ModuleType::Substation:
    substationLoop();
    break;
  case ModuleType::Gateway:
    gatewayLoop();
    break;
  default: {
    // Unknown module type: stay idle, remind on serial every 5 s.
    static unsigned long lastReminder = 0;
    if (millis() - lastReminder >= 5000) {
      lastReminder = millis();
      Serial.println("[BOOT] Unknown module type. Idling.");
    }
    break;
  }
  }
}
