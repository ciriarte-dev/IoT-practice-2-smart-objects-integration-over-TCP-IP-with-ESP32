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
  static const char* kSteps[] = {"LED RED BLINK", "LED YELLOW ON", "LED GREEN ON", "LED ALL OFF", "FOO"};
  static uint8_t index = 0;
  static uint32_t lastMs = 0;
  static bool first = true;

  if (first || millis() - lastMs >= 4000) {
    first = false;
    lastMs = millis();
    const char* text = kSteps[index];
    index = (index + 1) % (sizeof(kSteps) / sizeof(kSteps[0]));

    Serial.printf("[TEST] Comando: %s\n", text);
    if (!actuators.apply(CommandParser::parse(text))) {
      Serial.println("[TEST] Inválido: todos los LEDs apagados");
    }
  }
}
#endif

void setup() {
  Serial.begin(cfg::SERIAL_BAUD);
  actuators.begin();  // Apagar inmediatamente, antes de cualquier conexión.
  Serial.println("\n=== Actuador ESP32 (3 LEDs) ===");
#ifndef SELF_TEST
  client.begin();
#endif
}

void loop() {
#ifdef SELF_TEST
  runSelfTest();
#else
  client.update();
#endif
  actuators.update();  // Parpadeo no bloqueante, tras comprobar seguridad de red.
  delay(1);  // Ceder CPU sin esperar el intervalo de parpadeo.
}
