#pragma once

#include <WiFi.h>

#include "SensorConfig.h"
#include "UltrasonicSensor.h"

class SensorClient {
public:
    SensorClient(const char* ssid, const char* password, const char* serverIp,
                 uint16_t port = 8080);
    void begin();
    void update();
    // Solo admite una lectura recien obtenida. No conserva lecturas sin conexion.
    bool sendMeasurement(const UltrasonicSensor::Measurement& measurement);

private:
    enum class State { Disabled, WifiWaiting, TcpWaiting, SendingHello, AwaitingOk, Ready };
    void startWifi();
    void disconnect(const char* reason);
    void readIncoming();
    void handleLine();
    bool queueLine(const char* line);
    void flushOutgoing();

    const char* ssid_;
    const char* password_;
    const char* serverIp_;
    uint16_t port_;
    IPAddress address_;
    WiFiClient client_;
    State state_ = State::Disabled;
    uint32_t lastAttemptMs_ = 0;
    uint32_t handshakeSinceMs_ = 0;
    uint32_t sendSinceMs_ = 0;
    char incoming_[sensorConfig::maxLineBytes + 1] = {};
    size_t incomingLength_ = 0;
    char outgoing_[sensorConfig::maxLineBytes + 2] = {};
    size_t outgoingLength_ = 0;
    size_t outgoingOffset_ = 0;
};
