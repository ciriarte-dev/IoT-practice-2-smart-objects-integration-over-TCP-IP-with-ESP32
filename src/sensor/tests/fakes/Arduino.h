#pragma once
#include <cstddef>
#include <cstdint>

constexpr uint8_t OUTPUT = 1, INPUT = 0, LOW = 0, HIGH = 1;
unsigned long millis();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout);
struct FakeSerial {
    void begin(unsigned long) {}
    void println(const char* text);
    int printf(const char* format, ...);
};
extern FakeSerial Serial;
