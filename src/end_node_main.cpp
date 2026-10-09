/**
 * @file
 * End node firmware entry point: picks this board's meter node role and runs
 * that node.
 *
 * Every end node board runs the same firmware. setup() asks which role the
 * board plays, then calls that node's setup function, and loop() keeps calling
 * that node's loop function.
 */

#include <Arduino.h>
#include <cstdint>
#include "nodes/nodes.h"
#include "nvs_config.h"

/** Role an end node board plays in the network. The values match the boot menu keys. */
enum class ModuleType : uint8_t {
  Unknown = 0,     ///< No valid role chosen. The board idles.
  Rs485Node = 1,   ///< Meter node reading over RS485 (Modbus).
  IrNode = 2,      ///< Meter node reading the optical port (IEC 62056-21).
  CvNode = 3,      ///< Meter node reading the display with a camera.
};

/**
 * Asks over serial which role this board plays, blocking until a valid key.
 *
 * @return The chosen role.
 * @todo Read the board's module ID pin instead (driving GPIO3 and reading the
 *       voltage on GPIO4) once the final PCB exists.
 */
static ModuleType readModuleType() {
  Serial.println("[BOOT] Select module to test:");
  Serial.println("  1: RS485 node");
  Serial.println("  2: IR node");
  Serial.println("  3: CV node");

  while (true) {
    if (!Serial.available()) {
      delay(10);
      continue;
    }
    char c = Serial.read();
    if (c >= '1' && c <= '3') {
      return static_cast<ModuleType>(c - '0');
    }
    if (c != '\n' && c != '\r') {
      Serial.printf("[BOOT] '%c' is not 1-3\n", c);
    }
  }
}

static ModuleType moduleType = ModuleType::Unknown;  ///< Role chosen at boot.

/** Arduino entry point. Opens serial, picks the role and sets that node up. */
void setup() {
  Serial.begin(115200);

  while(!Serial){delay(100);} // SHOULD BE REMOVED IN PRODUCTION FOR DEEP SLEEP TO WORK PROPERLY
  delay(2000);

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
  default:
    Serial.println("[BOOT] Unknown module type. Idling.");
    break;
  }
}

/** Arduino main loop. Handles serial config commands, then runs the node. */
void loop() {
  nvsConfigPollSerial();

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