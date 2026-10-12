#pragma once

#include <WiFi.h>

#include "ActuatorConfig.h"
#include "ActuatorManager.h"

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
