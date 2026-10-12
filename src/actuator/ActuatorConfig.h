#pragma once

#include <Arduino.h>

namespace cfg {

// ---- Pines GPIO (cada LED con resistencia de 220 ohm hacia GND) ----
constexpr uint8_t PIN_RED    = 25;
constexpr uint8_t PIN_YELLOW = 26;
constexpr uint8_t PIN_GREEN  = 27;

// ---- Comportamiento de los LEDs ----
constexpr uint32_t BLINK_INTERVAL_MS = 250;  ///< Medio periodo del parpadeo rojo.

// ---- Red ----
constexpr uint32_t WIFI_RETRY_MS         = 10000;  ///< Reintento de WiFi.begin().
constexpr uint32_t TCP_RETRY_MS          = 2000;   ///< Espera entre reintentos TCP.
constexpr uint32_t TCP_CONNECT_TIMEOUT_MS = 2000;  ///< Timeout de connect().

// ---- Protocolo ----
constexpr size_t MAX_LINE_LENGTH = 32;  ///< Tamaño máximo de un comando (con '\0').
constexpr bool   SEND_HELLO      = true;   ///< Enviar identificación al conectar.
constexpr bool   SEND_REPLIES    = true;   ///< Responder OK/ERR a cada comando.
constexpr const char* HELLO_MSG  = "HELLO ACTUATOR";

// ---- Serial ----
constexpr unsigned long SERIAL_BAUD = 115200;

}  // namespace cfg
