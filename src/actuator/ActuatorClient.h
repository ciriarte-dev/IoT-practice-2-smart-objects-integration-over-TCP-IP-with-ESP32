/**
 * @file ActuatorClient.h
 * @brief Conexión WiFi + cliente TCP con reconexión automática.
 */
#pragma once

#include <WiFi.h>

#include "ActuatorConfig.h"
#include "ActuatorManager.h"

/**
 * @class ActuatorClient
 * @brief Máquina de estados no bloqueante:
 *        WIFI_CONNECTING -> TCP_CONNECTING -> CONNECTED -> (RETRY_WAIT) -> ...
 *
 * Mientras no hay conexión con el servidor, los LEDs quedan apagados
 * (estado seguro). Por eso el connect() con timeout no afecta al parpadeo.
 */
class ActuatorClient {
public:
  explicit ActuatorClient(ActuatorManager& actuators);

  /** Inicia la conexión WiFi. Llamar una vez en setup(). */
  void begin();

  /** Avanza la máquina de estados. Llamar en cada loop(). */
  void update();

private:
  enum class State : uint8_t { WIFI_CONNECTING, TCP_CONNECTING, CONNECTED, RETRY_WAIT };

  void setState(State next);
  void startWifi();
  void onConnectionLost(const char* reason);
  void readIncoming();
  void handleLine(const char* line);
  void sendLine(const char* text);

  ActuatorManager& actuators_;
  WiFiClient client_;
  State state_ = State::WIFI_CONNECTING;
  uint32_t stateSinceMs_ = 0;

  char buffer_[cfg::MAX_LINE_LENGTH] = {0};
  size_t length_ = 0;
  bool overflow_ = false;
};
