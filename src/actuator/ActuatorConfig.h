/**
 * @file ActuatorConfig.h
 * @brief Constantes de configuración del objeto inteligente actuador (3 LEDs).
 *
 * Las credenciales WiFi y la IP/puerto del servidor NO están aquí:
 * viven en "secrets.h" (ignorado por Git). Ver "secrets.example.h".
 */
#pragma once

#include <Arduino.h>

namespace cfg {

// ---- Pines GPIO (cada LED con resistencia de 220 ohm hacia GND) ----
constexpr uint8_t PIN_RED    = 4;   // D4
constexpr uint8_t PIN_YELLOW = 2;   // D2
constexpr uint8_t PIN_GREEN  = 15;  // D15

// ---- Comportamiento de los LEDs ----
constexpr uint32_t BLINK_INTERVAL_MS = 500;  ///< 500 ms encendido / 500 ms apagado.

// ---- Red ----
constexpr uint32_t WIFI_RETRY_MS         = 2000;  ///< Reintento de WiFi.begin().
constexpr uint32_t TCP_RETRY_MS          = 2000;   ///< Espera entre reintentos TCP.
constexpr uint32_t TCP_CONNECT_TIMEOUT_MS = 100;  ///< Espera acotada con LEDs apagados.
constexpr uint32_t HELLO_SEND_TIMEOUT_MS = 200;
constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 5000;
constexpr uint32_t COMMAND_TIMEOUT_MS = 3000;

// ---- Protocolo ----
constexpr size_t MAX_LINE_LENGTH = 128;  ///< Contenido, sin LF ni terminador C.
constexpr size_t READ_BUDGET_BYTES = 256; ///< Limitar trabajo por iteración.
constexpr const char* HELLO_MSG  = "HELLO ACTUATOR\n";

// ---- Serial ----
constexpr unsigned long SERIAL_BAUD = 115200;

}  // namespace cfg
