#include "UltrasonicSensor.h"

#include <math.h>

UltrasonicSensor::UltrasonicSensor(uint8_t triggerPin, uint8_t echoPin)
    : triggerPin_(triggerPin), echoPin_(echoPin) {}

void UltrasonicSensor::begin() {
    pinMode(triggerPin_, OUTPUT);
    digitalWrite(triggerPin_, LOW);
    pinMode(echoPin_, INPUT);
}

UltrasonicSensor::Measurement UltrasonicSensor::measure() const {
    digitalWrite(triggerPin_, LOW);
    delayMicroseconds(2);
    digitalWrite(triggerPin_, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin_, LOW);

    // Espera acotada: tambien termina si ECHO permanece alto o bajo.
    const unsigned long durationUs = pulseIn(echoPin_, HIGH, kEchoTimeoutUs);
    if (durationUs == 0) {
        return {Status::EchoTimeout, NAN, 0};
    }

    // Conversion indicada por la ficha del HC-SR04; incluye ida y vuelta.
    const float distanceCm = static_cast<float>(durationUs) / 58.0f;
    if (distanceCm < kMinDistanceCm || distanceCm > kMaxDistanceCm) {
        return {Status::OutOfRange, NAN, durationUs};
    }

    return {Status::Ok, distanceCm, durationUs};
}
