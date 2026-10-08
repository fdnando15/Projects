// config.h - Pines y constantes del proyecto.
//
// Unico sitio donde se definen numeros de pin y tiempos. El resto del codigo
// solo usa estos nombres, asi que cambiar el cableado no obliga a tocar main.cpp.
//
// Placa: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
//
// PINES PROHIBIDOS en esta placa (ya ocupados por el hardware integrado):
//   GPIO 8-14       -> SX1262 (LoRa): NSS 8, SCK 9, MOSI 10, MISO 11,
//                      RST 12, BUSY 13, DIO1 14
//   GPIO 17, 18, 21 -> OLED (SDA 17, SCL 18, RST 21)
//   GPIO 35         -> LED de la placa
//   GPIO 26-37      -> flash / PSRAM del ESP32-S3 (usarlos cuelga la placa)
// Libres y verificados para este proyecto: GPIO 4, 5, 6, 7.

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Puerto serie
// ---------------------------------------------------------------------------
constexpr uint32_t SERIAL_BAUD = 115200;

// ---------------------------------------------------------------------------
// Modulo de rele (4 canales, optoacoplado, VCC a 5 V, GND comun)
// ---------------------------------------------------------------------------
// La mayoria de modulos de rele baratos se activan con IN = GND (activo en
// bajo). Si el modulo fuese activo en alto, basta con poner esto a false:
// RelayController se encarga de invertir la logica.
constexpr bool RELAY_ACTIVE_LOW = true;

// Estado del rele nada mas arrancar. En un actuador conectado a red lo seguro
// es arrancar desconectado.
constexpr bool RELAY_DEFAULT_ON = false;

// Canal 1: unico conectado en la fase 1.
constexpr uint8_t RELAY_PIN_CH1 = 4;

// Canales 2-4: aun sin cablear. Los pines libres restantes son 5 y 7, asi
// que para el canal 4 habra que verificar otro GPIO (p. ej. 3 o 19) contra
// la lista de prohibidos antes de usarlo. Descomentar segun se conecten y
// crear una instancia de RelayController por canal en main.cpp.
// constexpr uint8_t RELAY_PIN_CH2 = 5;
// constexpr uint8_t RELAY_PIN_CH3 = 7;
// constexpr uint8_t RELAY_PIN_CH4 = ...;

// ---------------------------------------------------------------------------
// Pulsador (contra GND, con pull-up interno)
// ---------------------------------------------------------------------------
// Con pull-up interno el pin lee HIGH en reposo y LOW al pulsar. Si el
// pulsador no esta montado el pin queda fijo en HIGH y nunca hay flanco, asi
// que el codigo funciona igual sin el.
constexpr uint8_t  BUTTON_PIN         = 6;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 250;
