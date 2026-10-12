#pragma once

#include <Arduino.h>

class UltrasonicSensor {
public:
    enum class Status {
        Ok,
        EchoTimeout,
        OutOfRange
    };

    struct Measurement {
        Status status;
        // NAN cuando la medicion no es valida; consultar status antes de usarla.
        float distanceCm;
        unsigned long echoDurationUs;
    };

    UltrasonicSensor(uint8_t triggerPin, uint8_t echoPin);

    void begin();
    // Llamar despues de begin(), dejando al menos 60 ms entre mediciones.
    Measurement measure() const;

private:
    static constexpr unsigned long kEchoTimeoutUs = 30000UL;
    static constexpr float kMinDistanceCm = 2.0f;
    static constexpr float kMaxDistanceCm = 400.0f;

    const uint8_t triggerPin_;
    const uint8_t echoPin_;
};
