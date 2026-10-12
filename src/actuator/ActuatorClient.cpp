#include "ActuatorClient.h"

#include <errno.h>
#include <string.h>
#include <lwip/sockets.h>

#include "secrets.h"

ActuatorClient::ActuatorClient(ActuatorManager& actuators)
    : ActuatorClient(actuators, WIFI_SSID, WIFI_PASSWORD, SERVER_IP, SERVER_PORT) {}

ActuatorClient::ActuatorClient(ActuatorManager& actuators, const char* ssid,
                               const char* password, const char* serverIp, uint16_t port)
    : actuators_(actuators), ssid_(ssid), password_(password), serverIp_(serverIp), port_(port) {}

void ActuatorClient::begin() {
  actuators_.safeState();
  if (ssid_[0] == '\0' || !address_.fromString(serverIp_) ||
      address_ == IPAddress(0, 0, 0, 0) || port_ == 0) {
    Serial.println("[CONFIG] Completar secrets.h; red deshabilitada, LEDs apagados");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  startWifi();
}

void ActuatorClient::update() {
  if (state_ == State::CONFIG_MISSING) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (state_ != State::WIFI_CONNECTING) onConnectionLost("WiFi perdido");
    if (static_cast<uint32_t>(millis() - stateSinceMs_) >= cfg::WIFI_RETRY_MS) startWifi();
    return;
  }

  if (state_ == State::WIFI_CONNECTING) {
    Serial.println("[WIFI] Conectado");
    setState(State::TCP_CONNECTING);
  }
  if (state_ == State::RETRY_WAIT) {
    if (static_cast<uint32_t>(millis() - stateSinceMs_) < cfg::TCP_RETRY_MS) return;
    setState(State::TCP_CONNECTING);
  }
  if (state_ == State::TCP_CONNECTING) {
    actuators_.safeState();
    Serial.printf("[TCP] Conectando a %s:%u ...\n", serverIp_, port_);
    if (!client_.connect(address_, port_, cfg::TCP_CONNECT_TIMEOUT_MS)) {
      onConnectionLost("No se pudo conectar");
      return;
    }
    client_.setNoDelay(true);
    length_ = 0;
    helloOffset_ = 0;
    handshakeSinceMs_ = millis();
    Serial.println("[TCP] Conectado; identificando actuador");
    setState(State::SENDING_HELLO);
  }

  // Revisar la conexión y el timeout antes de consumir posibles datos pendientes.
  if (!client_.connected()) {
    onConnectionLost("Servidor desconectado");
    return;
  }
  if ((state_ == State::SENDING_HELLO || state_ == State::WAIT_OK) &&
      static_cast<uint32_t>(millis() - handshakeSinceMs_) >= cfg::HANDSHAKE_TIMEOUT_MS) {
    onConnectionLost("Timeout esperando OK");
    return;
  }
  if (state_ == State::CONNECTED &&
      static_cast<uint32_t>(millis() - lastCommandMs_) >= cfg::COMMAND_TIMEOUT_MS) {
    // Cerrar descarta también comandos antiguos que pudieran quedar en el socket.
    onConnectionLost("3 segundos sin comandos validos");
    return;
  }
  if (state_ == State::SENDING_HELLO) sendHello();
  if (state_ == State::WAIT_OK || state_ == State::CONNECTED) readIncoming();
}

void ActuatorClient::setState(State next) {
  state_ = next;
  stateSinceMs_ = millis();
}

void ActuatorClient::startWifi() {
  actuators_.safeState();
  WiFi.disconnect();
  WiFi.begin(ssid_, password_);
  Serial.println("[WIFI] Intentando conectar");
  setState(State::WIFI_CONNECTING);
}

void ActuatorClient::onConnectionLost(const char* reason) {
  actuators_.safeState();
  client_.stop();
  length_ = 0;
  helloOffset_ = 0;
  setState(WiFi.status() == WL_CONNECTED ? State::RETRY_WAIT : State::WIFI_CONNECTING);
  Serial.printf("[NET] %s; LEDs apagados; reintento en 2000 ms\n", reason);
}

void ActuatorClient::sendHello() {
  if (static_cast<uint32_t>(millis() - stateSinceMs_) >= cfg::HELLO_SEND_TIMEOUT_MS) {
    onConnectionLost("Timeout enviando HELLO");
    return;
  }
  const size_t length = strlen(cfg::HELLO_MSG);
  // Evitar los reintentos bloqueantes de WiFiClient::write. Solo se transmite
  // HELLO, con soporte de escrituras parciales sobre el socket de WiFiClient.
  const int sent = send(client_.fd(), cfg::HELLO_MSG + helloOffset_,
                        length - helloOffset_, MSG_DONTWAIT);
  if (sent < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      onConnectionLost("Error enviando HELLO");
    }
    return;
  }
  if (sent == 0) {
    onConnectionLost("Envio sin progreso");
    return;
  }
  helloOffset_ += static_cast<size_t>(sent);
  if (helloOffset_ == length) {
    Serial.printf("[TX] %s", cfg::HELLO_MSG);
    setState(State::WAIT_OK);
  }
}

void ActuatorClient::readIncoming() {
  for (size_t count = 0; count < cfg::READ_BUDGET_BYTES && client_.available() > 0; ++count) {
    const int c = client_.read();
    if (c < 0) return;
    if (c == '\n') {
      buffer_[length_] = '\0';
      handleLine(buffer_);
      length_ = 0;
      if (state_ != State::WAIT_OK && state_ != State::CONNECTED) return;
    } else if (c < 32 || c > 126 || length_ >= cfg::MAX_LINE_LENGTH) {
      onConnectionLost("Mensaje no ASCII valido o demasiado largo");
      return;
    } else {
      buffer_[length_++] = static_cast<char>(c);
    }
  }
}

void ActuatorClient::handleLine(const char* line) {
  if (state_ == State::WAIT_OK) {
    if (strcmp(line, "OK") != 0) {
      onConnectionLost("Identificacion rechazada o respuesta previa a OK");
      return;
    }
    actuators_.safeState();
    lastCommandMs_ = millis();  // Plazo inicial, aún sin activar ningún LED.
    setState(State::CONNECTED);
    Serial.println("[TCP] OK recibido; esperando comandos");
    return;
  }

  const Command cmd = CommandParser::parse(line);
  if (!actuators_.apply(cmd)) {
    Serial.println("[RX] Comando invalido: LEDs apagados");
    // No renovar el watchdog, ni enviar ACK/ERR no previstos por el contrato.
    return;
  }
  lastCommandMs_ = millis();
  Serial.printf("[RX] %s\n", CommandParser::toText(cmd));
}
