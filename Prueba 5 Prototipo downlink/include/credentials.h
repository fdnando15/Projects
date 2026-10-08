#pragma once
#include <stdint.h>

// Credenciales OTAA de ChirpStack (formato MSB)

static const uint64_t LORAWAN_JOIN_EUI = 0x0000000000000000ULL;

static const uint64_t LORAWAN_DEV_EUI  = 0xF1C0D773FF5B7E7BULL;

static const uint8_t LORAWAN_APP_KEY[16] = {
  0xCC, 0xEA, 0xC0, 0x3B, 0xBC, 0xAA, 0x8F, 0x05,
  0xD3, 0x55, 0xB1, 0x84, 0xDA, 0xB2, 0xDE, 0x44
};
