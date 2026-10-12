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
 * @brief Máquina de estados con conexión acotada y E/S no bloqueante:
 *        WIFI_CONNECTING -> TCP_CONNECTING -> SENDING_HELLO -> WAIT_OK -> CONNECTED.
 *
 * Mientras no hay conexión con el servidor, los LEDs quedan apagados
 * (estado seguro). Por eso el connect() con timeout no afecta al parpadeo.
 */
class ActuatorClient {
public:
  explicit ActuatorClient(ActuatorManager& actuators);
  ActuatorClient(ActuatorManager& actuators, const char* ssid, const char* password,
                 const char* serverIp, uint16_t port = 8080);

  /** Inicia la conexión WiFi. Llamar una vez en setup(). */
  void begin();

  /** Avanza la máquina de estados. Llamar en cada loop(). */
  void update();

private:
  enum class State : uint8_t {
    CONFIG_MISSING, WIFI_CONNECTING, TCP_CONNECTING, SENDING_HELLO, WAIT_OK, CONNECTED, RETRY_WAIT
  };

  void setState(State next);
  void startWifi();
  void onConnectionLost(const char* reason);
  void readIncoming();
  void handleLine(const char* line);
  void sendHello();

  ActuatorManager& actuators_;
  WiFiClient client_;
  const char* ssid_;
  const char* password_;
  const char* serverIp_;
  uint16_t port_;
  IPAddress address_;
  State state_ = State::CONFIG_MISSING;
  uint32_t stateSinceMs_ = 0;
  uint32_t handshakeSinceMs_ = 0;
  uint32_t lastCommandMs_ = 0;
  size_t helloOffset_ = 0;

  char buffer_[cfg::MAX_LINE_LENGTH + 1] = {0};
  size_t length_ = 0;
};
