#pragma once

#include <Arduino.h>

class Led {
public:
  enum class Mode : uint8_t { OFF, ON, BLINK };

  Led(uint8_t pin, const char* name);

  /** Configura el pin como salida y deja el LED apagado. */
  void begin();

  /** Enciende el LED de forma fija. */
  void on();

  /** Apaga el LED y cancela cualquier parpadeo. */
  void off();

  void startBlink(uint32_t intervalMs);

  /** Avanza el parpadeo. Llamar en cada loop(). */
  void update();

  Mode mode() const { return mode_; }
  const char* name() const { return name_; }

  /** @return "OFF", "ON" o "BLINK". */
  const char* modeText() const;

private:
  void write(bool level);

  uint8_t pin_;
  const char* name_;
  Mode mode_ = Mode::OFF;
  bool level_ = false;
  uint32_t intervalMs_ = 0;
  uint32_t lastToggleMs_ = 0;
};
