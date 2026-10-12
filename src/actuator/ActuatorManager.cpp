#include "ActuatorManager.h"

#include "ActuatorConfig.h"

ActuatorManager::ActuatorManager()
    : red_(cfg::PIN_RED),
      yellow_(cfg::PIN_YELLOW),
      green_(cfg::PIN_GREEN) {}

void ActuatorManager::begin() {
  red_.begin();
  yellow_.begin();
  green_.begin();
  current_ = Command::INVALID;
  printStatus();
}

bool ActuatorManager::apply(Command cmd) {
  if (cmd == Command::INVALID) {
    safeState();
    return false;
  }
  if (cmd == current_) {
    return true;  // Ya activo: no tocar (evita reiniciar el parpadeo).
  }

  allOff();
  switch (cmd) {
    case Command::RED_BLINK: red_.startBlink(cfg::BLINK_INTERVAL_MS); break;
    case Command::YELLOW_ON: yellow_.on(); break;
    case Command::GREEN_ON:  green_.on(); break;
    case Command::ALL_OFF: break;  // allOff() ya apagó los tres LEDs.
    default: break;
  }
  current_ = cmd;
  printStatus();
  return true;
}

void ActuatorManager::safeState() {
  allOff();
  if (current_ != Command::INVALID) {
    current_ = Command::INVALID;
    Serial.println("[SAFE] Estado seguro: todos los LEDs apagados");
    printStatus();
  }
}

void ActuatorManager::update() {
  red_.update();
  yellow_.update();
  green_.update();
}

void ActuatorManager::printStatus() const {
  Serial.printf("[LEDS] ROJO=%s AMARILLO=%s VERDE=%s\n", red_.modeText(),
                yellow_.modeText(), green_.modeText());
}

void ActuatorManager::allOff() {
  red_.off();
  yellow_.off();
  green_.off();
}
