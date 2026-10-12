/**
 * @file CommandParser.h
 * @brief Traduce una línea de texto recibida por TCP a un comando tipado.
 */
#pragma once

#include <Arduino.h>

/** Comandos válidos del protocolo del actuador. */
enum class Command : uint8_t {
  INVALID,     ///< Texto no reconocido (o "ningún comando activo").
  RED_BLINK,   ///< LED rojo parpadeando.
  YELLOW_ON,   ///< LED amarillo encendido.
  GREEN_ON,    ///< LED verde encendido.
  ALL_OFF      ///< Todos apagados por orden explícita del servidor.
};

/**
 * @class CommandParser
 * @brief Valida el texto del servidor. No conoce distancias ni rangos:
 *        solo reconoce las palabras del protocolo.
 */
class CommandParser {
public:
  /**
   * Interpreta una línea (sin '\n') con coincidencia exacta ASCII.
   * No acepta espacios adicionales ni cambios de mayúsculas.
   * @return El comando, o Command::INVALID si no coincide con ninguno.
   */
  static Command parse(const char* text);

  /** @return Nombre del comando como texto (para logs y respuestas). */
  static const char* toText(Command cmd);
};
