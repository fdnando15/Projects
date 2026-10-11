#ifndef CONFIG_H
#define CONFIG_H

#include <RadioLib.h>

// ---- Credenciales ChirpStack (formato MSB) ----
#define JOIN_EUI  0x0000000000000000

#define DEV_EUI   0xE1C0D773FF5B7E7B

#define APP_KEY   0x4F, 0x0B, 0xA4, 0x3D, 0xD2, 0xD6, 0xF3, 0xD2, 0xFC, 0x79, 0x86, 0x55, 0xE4, 0xC9, 0x19, 0x94

#define NWK_KEY   APP_KEY   // no se usa en LoRaWAN 1.0.x

const LoRaWANBand_t Region = EU868;
const uint8_t subBand = 0;

// Intervalo entre uplinks (duty cycle 1%)
const uint32_t uplinkIntervalSeconds = 60UL;

// DevNonce de partida si la flash esta vacia, por encima de los ya usados
const uint16_t devNonceStart = 1000;

const uint8_t uplinkPort = 10;

// ---- Hardware ----
#define DHT_PIN   4
#define DHT_TYPE  DHT11

// SX1262: NSS, DIO1, RST, BUSY
SX1262 radio = new Module(SS, DIO0, RST_LoRa, BUSY_LoRa);

LoRaWANNode node(&radio, &Region, subBand);

// ---- Depuracion ----
String stateDecode(const int16_t result) {
  switch (result) {
    case RADIOLIB_ERR_NONE:                 return "ERR_NONE";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:       return "ERR_CHIP_NOT_FOUND";
    case RADIOLIB_ERR_PACKET_TOO_LONG:      return "ERR_PACKET_TOO_LONG";
    case RADIOLIB_ERR_MIC_MISMATCH:         return "ERR_MIC_MISMATCH";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED:   return "ERR_NETWORK_NOT_JOINED";
    case RADIOLIB_ERR_NO_JOIN_ACCEPT:       return "ERR_NO_JOIN_ACCEPT";
    case RADIOLIB_ERR_JOIN_NONCE_INVALID:   return "ERR_JOIN_NONCE_INVALID";
    case RADIOLIB_ERR_UPLINK_UNAVAILABLE:   return "ERR_UPLINK_UNAVAILABLE";
    case RADIOLIB_ERR_DWELL_TIME_EXCEEDED:  return "ERR_DWELL_TIME_EXCEEDED";
    case RADIOLIB_LORAWAN_NEW_SESSION:      return "NEW_SESSION";
    case RADIOLIB_LORAWAN_SESSION_RESTORED: return "SESSION_RESTORED";
    case RADIOLIB_ERR_NONCES_DISCARDED:     return "ERR_NONCES_DISCARDED";
    case RADIOLIB_ERR_SESSION_DISCARDED:    return "ERR_SESSION_DISCARDED";
  }
  return String("codigo ") + result;
}

void debug(bool failed, const __FlashStringHelper* message, int16_t state, bool halt) {
  if (!failed) { return; }
  Serial.print(message);
  Serial.print(F(" -> "));
  Serial.print(stateDecode(state));
  Serial.print(F(" ("));
  Serial.print(state);
  Serial.println(F(")"));
  while (halt) { delay(10); }
}

#endif  // CONFIG_H
