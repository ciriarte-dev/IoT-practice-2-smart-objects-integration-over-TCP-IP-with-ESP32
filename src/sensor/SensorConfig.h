#pragma once

#include <Arduino.h>

namespace sensorConfig {
constexpr uint8_t triggerPin = 32;
constexpr uint8_t echoPin = 35;
constexpr uint32_t measurementIntervalMs = 500;
constexpr uint32_t retryIntervalMs = 2000;
constexpr uint32_t handshakeTimeoutMs = 5000;
constexpr uint32_t sendTimeoutMs = 200;
constexpr int32_t connectTimeoutMs = 100;
constexpr size_t maxLineBytes = 128;
constexpr size_t readBudgetBytes = 256;
}  // namespace sensorConfig
