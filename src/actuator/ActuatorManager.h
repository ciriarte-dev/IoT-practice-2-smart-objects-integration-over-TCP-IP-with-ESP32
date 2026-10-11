/**
 * @file ActuatorManager.h
 * @brief Gestiona los tres LEDs y garantiza que solo un comportamiento esté activo.
 */
#pragma once

#include "CommandParser.h"
#include "Led.h"

/**
 * @class ActuatorManager
 * @brief Ejecuta comandos sobre los LEDs rojo, amarillo y verde.
 *
 * No contiene lógica de distancias: solo ejecuta lo que el servidor ordena.
 */
class ActuatorManager {
public:
  ActuatorManager();

  /** Inicializa los pines y deja todos los LEDs apagados. */
  void begin();

  /**
   * Aplica un comando: apaga todo y activa únicamente el comportamiento pedido.
   * Si el comando es el que ya está activo, no hace nada (no reinicia el parpadeo).
   * @return false si el comando es INVALID (el estado actual no cambia).
   */
  bool apply(Command cmd);

  /** Estado seguro (conexión perdida): todos los LEDs apagados. */
  void safeState();

  /** Avanza el parpadeo. Llamar en cada loop(). */
  void update();

  /** @return Comando activo (INVALID si no hay ninguno / estado seguro). */
  Command current() const { return current_; }

  /** Imprime el estado de los tres LEDs en el monitor serial. */
  void printStatus() const;

private:
  void allOff();

  Led red_;
  Led yellow_;
  Led green_;
  Command current_ = Command::INVALID;
};
