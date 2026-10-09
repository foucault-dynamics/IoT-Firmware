/**
 * @file
 * LoRa PCB firmware entry point: picks whether this board is a substation or a
 * gateway and runs that node.
 *
 * Every LoRa PCB runs the same firmware. setup() asks which role the board
 * plays, then calls that node's setup function, and loop() keeps calling that
 * node's loop function.
 */

#include <Arduino.h>
#include <cstdint>
#include "nodes/nodes.h"
#include "nvs_config.h"

/** Role a LoRa PCB plays in the network. The values match the boot menu keys. */
enum class ModuleType : uint8_t {
  Unknown = 0,     ///< No valid role chosen. The board idles.
  Substation = 4,  ///< Relays meter readings from ESP-NOW to LoRa.
  Gateway = 5,     ///< Receives over LoRa and publishes to MQTT.
};

/**
 * Asks over serial which role this board plays, blocking until a valid key.
 *
 * @return The chosen role.
 * @todo Read whatever ID mechanism the hardware team puts on the LoRa PCB
 *       instead.
 */
static ModuleType readModuleType() {
  Serial.println("[BOOT] Select module to test:");
  Serial.println("  4: Substation");
  Serial.println("  5: Gateway");

  while (true) {
    if (!Serial.available()) {
      delay(10);
      continue;
    }
    char c = Serial.read();
    if (c == '4' || c == '5') {
      return static_cast<ModuleType>(c - '0');
    }
    if (c != '\n' && c != '\r') {
      Serial.printf("[BOOT] '%c' is not 4 or 5\n", c);
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

/** Arduino main loop. Handles serial config commands, then runs the node. */
void loop() {
  nvsConfigPollSerial();

  switch (moduleType) {
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