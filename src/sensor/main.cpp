#include <Arduino.h>

#include "UltrasonicSensor.h"

namespace {
constexpr uint8_t kTriggerPin = 4;  // D4
constexpr uint8_t kEchoPin = 2;     // D2
constexpr uint32_t kMeasurementIntervalMs = 500;

UltrasonicSensor sensor(kTriggerPin, kEchoPin);
uint32_t lastMeasurementMs = 0;
}  // namespace

void setup() {
    Serial.begin(115200);
    sensor.begin();
    Serial.println("HC-SR04 | TRIG=4 | ECHO=2 | intervalo=500 ms");
    lastMeasurementMs = millis();
}

void loop() {
    const uint32_t now = millis();
    // La resta sin signo mantiene el intervalo al desbordarse millis().
    if (static_cast<uint32_t>(now - lastMeasurementMs) < kMeasurementIntervalMs) {
        delay(1);  // Ceder CPU sin bloquear durante los 500 ms del intervalo.
        return;
    }
    // Tomar el inicio de la lectura evita sumar su duracion al intervalo.
    // No acumular ni ejecutar rafagas de lecturas si el loop se retrasa.
    lastMeasurementMs = now;
    const UltrasonicSensor::Measurement measurement = sensor.measure();

    Serial.printf("[%lu ms] ", static_cast<unsigned long>(now));
    switch (measurement.status) {
        case UltrasonicSensor::Status::Ok:
            Serial.printf("Distancia: %.2f cm\n", measurement.distanceCm);
            break;
        case UltrasonicSensor::Status::EchoTimeout:
            Serial.println("ERROR: sin pulso ECHO completo en 30 ms");
            break;
        case UltrasonicSensor::Status::OutOfRange:
            Serial.printf("ERROR: medicion fuera de 2-400 cm (ECHO=%lu us)\n",
                          measurement.echoDurationUs);
            break;
    }
}
