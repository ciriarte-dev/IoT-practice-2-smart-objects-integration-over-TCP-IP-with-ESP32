#include "SensorClient.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <lwip/sockets.h>

SensorClient::SensorClient(const char* ssid, const char* password,
                           const char* serverIp, uint16_t port)
    : ssid_(ssid), password_(password), serverIp_(serverIp), port_(port) {}

void SensorClient::begin() {
    if (ssid_[0] == '\0' || !address_.fromString(serverIp_) ||
        address_ == IPAddress(0, 0, 0, 0) || port_ == 0) {
        Serial.println("[CONFIG] Completar secrets.h con WiFi e IPv4 del servidor; solo lectura local");
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);  // Los reintentos los controla esta maquina de estados.
    state_ = State::WifiWaiting;
    startWifi();
}

void SensorClient::startWifi() {
    lastAttemptMs_ = millis();
    WiFi.disconnect();
    WiFi.begin(ssid_, password_);
    Serial.println("[WIFI] Intentando conectar");
}

void SensorClient::disconnect(const char* reason) {
    Serial.printf("[TCP] %s; reintento en 2000 ms\n", reason);
    client_.stop();
    incomingLength_ = 0;
    outgoingLength_ = 0;
    outgoingOffset_ = 0;
    lastAttemptMs_ = millis();
    state_ = WiFi.status() == WL_CONNECTED ? State::TcpWaiting : State::WifiWaiting;
}

void SensorClient::update() {
    if (state_ == State::Disabled) return;

    if (WiFi.status() != WL_CONNECTED) {
        if (state_ != State::WifiWaiting) {
            Serial.println("[WIFI] Conexion perdida");
            disconnect("WiFi no disponible");
        }
        if (static_cast<uint32_t>(millis() - lastAttemptMs_) >= sensorConfig::retryIntervalMs) {
            startWifi();
        }
        return;
    }

    if (state_ == State::WifiWaiting) {
        Serial.println("[WIFI] Conectado");
        state_ = State::TcpWaiting;
        lastAttemptMs_ = millis() - sensorConfig::retryIntervalMs;
    }

    if (state_ == State::TcpWaiting) {
        if (static_cast<uint32_t>(millis() - lastAttemptMs_) < sensorConfig::retryIntervalMs) return;
        Serial.printf("[TCP] Conectando a %s:%u\n", serverIp_, port_);
        if (!client_.connect(address_, port_, sensorConfig::connectTimeoutMs)) {
            disconnect("No se pudo conectar");
            return;
        }
        client_.setNoDelay(true);
        Serial.println("[TCP] Conectado; enviando identificacion");
        incomingLength_ = 0;
        handshakeSinceMs_ = millis();
        state_ = State::SendingHello;
        queueLine("HELLO SENSOR");
    }

    if (!client_.connected()) {
        disconnect("Conexion cerrada");
        return;
    }
    if ((state_ == State::SendingHello || state_ == State::AwaitingOk) &&
        static_cast<uint32_t>(millis() - handshakeSinceMs_) >= sensorConfig::handshakeTimeoutMs) {
        disconnect("Timeout esperando OK");
        return;
    }

    flushOutgoing();
    if (state_ == State::AwaitingOk || state_ == State::Ready) readIncoming();
}

bool SensorClient::queueLine(const char* line) {
    const size_t length = strlen(line);
    if (outgoingLength_ != 0 || length > sensorConfig::maxLineBytes) return false;
    memcpy(outgoing_, line, length);
    outgoing_[length] = '\n';
    outgoing_[length + 1] = '\0';
    outgoingLength_ = length + 1;
    outgoingOffset_ = 0;
    sendSinceMs_ = millis();
    return true;
}

void SensorClient::flushOutgoing() {
    if (outgoingLength_ == 0) return;
    if (static_cast<uint32_t>(millis() - sendSinceMs_) >= sensorConfig::sendTimeoutMs) {
        disconnect("Timeout de envio; descartando trama pendiente");
        return;
    }
    // WiFiClient::write puede esperar varios segundos. Usar su socket con
    // MSG_DONTWAIT mantiene una sola escritura no bloqueante por iteracion.
    const int sent = send(client_.fd(), outgoing_ + outgoingOffset_,
                          outgoingLength_ - outgoingOffset_, MSG_DONTWAIT);
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            disconnect("Error de envio");
        }
        return;
    }
    if (sent == 0) {
        disconnect("Socket sin progreso de envio");
        return;
    }
    outgoingOffset_ += static_cast<size_t>(sent);
    if (outgoingOffset_ == outgoingLength_) {
        Serial.printf("[TX] %s", outgoing_);
        outgoingLength_ = 0;
        outgoingOffset_ = 0;
        if (state_ == State::SendingHello) state_ = State::AwaitingOk;
    }
}

void SensorClient::readIncoming() {
    for (size_t count = 0; count < sensorConfig::readBudgetBytes && client_.available() > 0; ++count) {
        const int byte = client_.read();
        if (byte < 0) return;
        if (byte == '\n') {
            incoming_[incomingLength_] = '\0';
            handleLine();
            incomingLength_ = 0;
            if (state_ != State::Ready && state_ != State::AwaitingOk) return;
        } else if (byte < 32 || byte > 126 || incomingLength_ >= sensorConfig::maxLineBytes) {
            disconnect("Respuesta no ASCII valida o linea demasiado larga");
            return;
        } else {
            incoming_[incomingLength_++] = static_cast<char>(byte);
        }
    }
}

void SensorClient::handleLine() {
    if (state_ == State::AwaitingOk && strcmp(incoming_, "OK") == 0) {
        state_ = State::Ready;
        Serial.println("[TCP] OK recibido; habilitado envio de nuevas mediciones");
        return;
    }
    // ERROR no confirma una lectura. Descartar la sesion ante rechazo del
    // servidor, OK repetido o cualquier respuesta fuera del contrato.
    Serial.printf("[RX] %s\n", incoming_);
    disconnect("Respuesta inesperada o rechazo del servidor");
}

bool SensorClient::sendMeasurement(const UltrasonicSensor::Measurement& measurement) {
    if (state_ != State::Ready) return false;
    if (WiFi.status() != WL_CONNECTED || !client_.connected()) {
        disconnect("Conexion perdida antes de enviar");
        return false;
    }
    if (outgoingLength_ != 0) {
        disconnect("Envio anterior pendiente; descartando mediciones");
        return false;
    }
    char line[32];
    if (measurement.status == UltrasonicSensor::Status::Ok &&
        isfinite(measurement.distanceCm) && measurement.distanceCm >= 2.0f &&
        measurement.distanceCm <= 400.0f) {
        snprintf(line, sizeof(line), "DIST %.2f", measurement.distanceCm);
    } else {
        snprintf(line, sizeof(line), "INVALID");
    }
    if (!queueLine(line)) return false;
    flushOutgoing();
    return state_ == State::Ready;
}
