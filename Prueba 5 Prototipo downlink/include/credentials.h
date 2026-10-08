#pragma once
#include <stdint.h>

// ============================================================================
//  Credenciales LoRaWAN (OTAA) — cópialas desde ChirpStack:
//    Applications -> tu aplicación -> tu dispositivo -> pestaña "OTAA keys"
//
//  IMPORTANTE:
//   - JoinEUI (AppEUI) y DevEUI se escriben como número de 64 bits, tal cual
//     los muestra ChirpStack (formato MSB, sin invertir el orden de bytes).
//   - AppKey son los 16 bytes en el mismo orden que los muestra ChirpStack.
//   - Con ChirpStack v4 y perfil LoRaWAN 1.0.x sólo hace falta AppKey.
//     Si usas 1.1.x, además necesitas NwkKey.
// ============================================================================

// JoinEUI: en ChirpStack v4 puede dejarse a 0 si no usas un Join Server externo.
static const uint64_t LORAWAN_JOIN_EUI = 0x0000000000000000ULL;

// DevEUI del dispositivo (ejemplo: 70B3D57ED0061234 -> 0x70B3D57ED0061234ULL)
static const uint64_t LORAWAN_DEV_EUI  = 0xF1C0D773FF5B7E7BULL;

// AppKey (16 bytes)
static const uint8_t LORAWAN_APP_KEY[16] = {
  0xCC, 0xEA, 0xC0, 0x3B, 0xBC, 0xAA, 0x8F, 0x05,
  0xD3, 0x55, 0xB1, 0x84, 0xDA, 0xB2, 0xDE, 0x44
};

// NwkKey: sólo para LoRaWAN 1.1. Con 1.0.x se pasa NULL (ver main.cpp).
// static const uint8_t LORAWAN_NWK_KEY[16] = { ... };
