/**
 * @file Led.h
 * @brief Encapsula un LED conectado a un pin GPIO: encendido, apagado y parpadeo.
 */
#pragma once

#include <Arduino.h>

/**
 * @class Led
 * @brief Un LED con tres modos (OFF, ON, BLINK).
 *
 * El parpadeo es NO bloqueante: se basa en millis() y requiere llamar a
 * update() en cada iteración de loop().
 */
class Led {
public:
  enum class Mode : uint8_t { OFF, ON, BLINK };

  /**
   * @param pin  Pin GPIO del LED.
   * @param name Nombre legible para el monitor serial.
   */
  Led(uint8_t pin, const char* name);

  /** Configura el pin como salida y deja el LED apagado. */
  void begin();

  /** Enciende el LED de forma fija. */
  void on();

  /** Apaga el LED y cancela cualquier parpadeo. */
  void off();

  /**
   * Inicia el parpadeo. Si ya parpadea con el mismo intervalo no hace nada
   * (así no se reinicia la fase si el servidor repite el comando).
   * @param intervalMs Duración de cada mitad del ciclo (encendido/apagado).
   */
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
