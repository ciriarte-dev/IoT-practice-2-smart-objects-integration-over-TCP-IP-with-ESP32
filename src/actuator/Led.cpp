#include "Led.h"

Led::Led(uint8_t pin) : pin_(pin) {}

void Led::begin() {
  pinMode(pin_, OUTPUT);
  off();
}

void Led::on() {
  mode_ = Mode::ON;
  write(true);
}

void Led::off() {
  mode_ = Mode::OFF;
  write(false);
}

void Led::startBlink(uint32_t intervalMs) {
  if (mode_ == Mode::BLINK && intervalMs_ == intervalMs) {
    return;  // Ya parpadea así: no reiniciar la fase.
  }
  mode_ = Mode::BLINK;
  intervalMs_ = intervalMs;
  lastToggleMs_ = millis();
  write(true);
}

void Led::update() {
  if (mode_ != Mode::BLINK) {
    return;
  }
  if (millis() - lastToggleMs_ >= intervalMs_) {
    lastToggleMs_ = millis();
    write(!level_);
  }
}

const char* Led::modeText() const {
  switch (mode_) {
    case Mode::ON:    return "ON";
    case Mode::BLINK: return "BLINK";
    default:          return "OFF";
  }
}

void Led::write(bool level) {
  level_ = level;
  digitalWrite(pin_, level ? HIGH : LOW);
}
