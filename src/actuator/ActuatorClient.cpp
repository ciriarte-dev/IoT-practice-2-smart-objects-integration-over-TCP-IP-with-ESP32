#include "ActuatorClient.h"

#include "secrets.h"

ActuatorClient::ActuatorClient(ActuatorManager& actuators) : actuators_(actuators) {}

void ActuatorClient::begin() {
  WiFi.mode(WIFI_STA);
  startWifi();
  setState(State::WIFI_CONNECTING);
}

void ActuatorClient::update() {
  // Si el WiFi se cae en cualquier estado, volver a empezar de forma segura.
  if (state_ != State::WIFI_CONNECTING && WiFi.status() != WL_CONNECTED) {
    onConnectionLost("WiFi perdido");
    startWifi();
    setState(State::WIFI_CONNECTING);
    return;
  }

  switch (state_) {
    case State::WIFI_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Conectado. IP local: %s\n", WiFi.localIP().toString().c_str());
        setState(State::TCP_CONNECTING);
      } else if (millis() - stateSinceMs_ >= cfg::WIFI_RETRY_MS) {
        Serial.println("[WIFI] Sin conexión, reintentando...");
        startWifi();
        stateSinceMs_ = millis();
      }
      break;

    case State::TCP_CONNECTING:
      Serial.printf("[TCP] Conectando a %s:%d ...\n", SERVER_IP, SERVER_PORT);
      if (client_.connect(SERVER_IP, SERVER_PORT, cfg::TCP_CONNECT_TIMEOUT_MS)) {
        client_.setNoDelay(true);
        length_ = 0;
        overflow_ = false;
        Serial.println("[TCP] Conectado al servidor");
        if (cfg::SEND_HELLO) {
          sendLine(cfg::HELLO_MSG);
        }
        setState(State::CONNECTED);
      } else {
        Serial.println("[TCP] No se pudo conectar");
        setState(State::RETRY_WAIT);
      }
      break;

    case State::RETRY_WAIT:
      if (millis() - stateSinceMs_ >= cfg::TCP_RETRY_MS) {
        setState(State::TCP_CONNECTING);
      }
      break;

    case State::CONNECTED:
      readIncoming();
      if (!client_.connected()) {
        onConnectionLost("Servidor desconectado");
        setState(State::RETRY_WAIT);
      }
      break;
  }
}

void ActuatorClient::setState(State next) {
  state_ = next;
  stateSinceMs_ = millis();
}

void ActuatorClient::startWifi() {
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Conectando a \"%s\" ...\n", WIFI_SSID);
}

void ActuatorClient::onConnectionLost(const char* reason) {
  Serial.printf("[NET] %s\n", reason);
  client_.stop();
  actuators_.safeState();  // Comportamiento seguro: todo apagado.
}

void ActuatorClient::readIncoming() {
  while (client_.available() > 0) {
    int c = client_.read();
    if (c < 0 || c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (overflow_) {
        Serial.println("[RX] Línea demasiado larga, descartada");
        if (cfg::SEND_REPLIES) sendLine("ERR LINE_TOO_LONG");
      } else if (length_ > 0) {
        buffer_[length_] = '\0';
        handleLine(buffer_);
      }
      length_ = 0;
      overflow_ = false;
      continue;
    }
    if (length_ < cfg::MAX_LINE_LENGTH - 1) {
      buffer_[length_++] = static_cast<char>(c);
    } else {
      overflow_ = true;
    }
  }
}

void ActuatorClient::handleLine(const char* line) {
  Serial.printf("[RX] \"%s\"\n", line);
  Command cmd = CommandParser::parse(line);

  if (!actuators_.apply(cmd)) {
    Serial.println("[RX] Comando inválido: se ignora, el estado no cambia");
    if (cfg::SEND_REPLIES) sendLine("ERR INVALID_COMMAND");
    return;
  }

  if (cfg::SEND_REPLIES) {
    char reply[cfg::MAX_LINE_LENGTH + 4];
    snprintf(reply, sizeof(reply), "OK %s", CommandParser::toText(cmd));
    sendLine(reply);
  }
}

void ActuatorClient::sendLine(const char* text) {
  client_.print(text);
  client_.print('\n');
}
