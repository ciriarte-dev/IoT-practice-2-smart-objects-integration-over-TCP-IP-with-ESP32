#include <Arduino.h>

#include "ActuatorConfig.h"
#include "ActuatorManager.h"

#ifndef SELF_TEST
#include "ActuatorClient.h"
#endif

ActuatorManager actuators;

#ifndef SELF_TEST
ActuatorClient client(actuators);
#else
/** Recorre cada comando cada 4 s e incluye uno inválido ("FOO"). */
static void runSelfTest() {
  static const char* kSteps[] = {"RED_BLINK", "YELLOW_ON", "GREEN_ON", "FOO"};
  static uint8_t index = 0;
  static uint32_t lastMs = 0;
  static bool first = true;

  if (first || millis() - lastMs >= 4000) {
    first = false;
    lastMs = millis();
    const char* text = kSteps[index];
    index = (index + 1) % 4;

    Serial.printf("[TEST] Comando: %s\n", text);
    if (!actuators.apply(CommandParser::parse(text))) {
      Serial.println("[TEST] Inválido: el estado no cambió");
    }
  }
}
#endif

void setup() {
  Serial.begin(cfg::SERIAL_BAUD);
  delay(300);
  Serial.println("\n=== Actuador ESP32 (3 LEDs) ===");
  actuators.begin();
#ifndef SELF_TEST
  client.begin();
#endif
}

void loop() {
  actuators.update();  // Parpadeo no bloqueante.
#ifdef SELF_TEST
  runSelfTest();
#else
  client.update();
#endif
}
