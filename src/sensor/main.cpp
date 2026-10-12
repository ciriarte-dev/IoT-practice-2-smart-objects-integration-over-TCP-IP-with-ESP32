#include <Arduino.h>

#include "UltrasonicSensor.h"
#include "SensorClient.h"
#include "SensorConfig.h"
#include "secrets.h"

namespace {
UltrasonicSensor sensor(sensorConfig::triggerPin, sensorConfig::echoPin);
SensorClient client(SENSOR_WIFI_SSID, SENSOR_WIFI_PASSWORD,
                    SENSOR_SERVER_IP, SENSOR_SERVER_PORT);
uint32_t lastMeasurementMs = 0;
}  // namespace

void setup() {
    Serial.begin(115200);
    sensor.begin();
    Serial.println("HC-SR04 | TRIG=32 | ECHO=35 | intervalo=500 ms");
    client.begin();
    lastMeasurementMs = millis();
}

void loop() {
    client.update();
    const uint32_t now = millis();
    // La resta sin signo mantiene el intervalo al desbordarse millis().
    if (static_cast<uint32_t>(now - lastMeasurementMs) < sensorConfig::measurementIntervalMs) {
        delay(1);  // Ceder CPU sin bloquear durante los 500 ms del intervalo.
        return;
    }
    // Tomar el inicio de la lectura evita sumar su duracion al intervalo.
    // No acumular ni ejecutar rafagas de lecturas si el loop se retrasa.
    lastMeasurementMs = now;
    const UltrasonicSensor::Measurement measurement = sensor.measure();
    client.sendMeasurement(measurement);

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
