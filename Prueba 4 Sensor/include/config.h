#ifndef CONFIG_H
#define CONFIG_H

#include <RadioLib.h>

// ===========================================================================
//  1. CREDENCIALES DE CHIRPSTACK  <-- ESTO ES LO UNICO QUE TIENES QUE TOCAR
// ===========================================================================
// En ChirpStack: Applications -> (tu app) -> Devices -> Add device
//   - Device EUI      -> DEV_EUI      (lo genera ChirpStack, 8 bytes)
//   - Join EUI/AppEUI -> JOIN_EUI     (ChirpStack lo ignora: deja 0 si quieres)
// Luego, en la pestana "OTAA keys" del dispositivo:
//   - Application key -> APP_KEY      (16 bytes)
// LoRaWAN 1.0.x (lo normal en ChirpStack) NO tiene Network key:
//   deja NWK_KEY igual que APP_KEY, RadioLib lo ignora.
// Formato: hexadecimal, en el MISMO orden que lo muestra ChirpStack (MSB).

#define JOIN_EUI  0x0000000000000000

#define DEV_EUI   0xE1C0D773FF5B7E7B

#define APP_KEY   0x4F, 0x0B, 0xA4, 0x3D, 0xD2, 0xD6, 0xF3, 0xD2, 0xFC, 0x79, 0x86, 0x55, 0xE4, 0xC9, 0x19, 0x94

#define NWK_KEY   APP_KEY

// Region: EU868 en Espana. Otras: US915, AU915, AS923, IN865, KR920, CN470
const LoRaWANBand_t Region = EU868;
const uint8_t subBand = 0;              // 0 para EU868; 2 para US915/AU915

// Cada cuanto se envia un uplink. Con SF alto y duty cycle del 1% en EU868
// no bajes de ~60 s en pruebas, y usa 5-15 min en produccion.
const uint32_t uplinkIntervalSeconds = 60UL;

// DevNonce inicial cuando la flash esta vacia. ChirpStack rechaza cualquier
// DevNonce que ya haya visto para este DevEUI, asi que si el dispositivo ya
// hizo joins antes (otro firmware, flash borrada...) sube este numero por
// encima del ultimo usado. Solo se aplica en el primer arranque.
const uint16_t devNonceStart = 1000;

// Puerto de aplicacion (fPort) con el que llegara la trama a ChirpStack
const uint8_t uplinkPort = 10;

// ===========================================================================
//  2. HARDWARE
// ===========================================================================
// DHT11: VCC -> 3V3, GND -> GND, DATA -> GPIO definido aqui.
// Si tu modulo es de 3 patillas ya lleva la resistencia de pull-up incorporada;
// si es el sensor "pelado" de 4 patillas, pon 4.7k-10k entre DATA y 3V3.
#define DHT_PIN   4
#define DHT_TYPE  DHT11

// Radio SX1262 de la Heltec WiFi LoRa 32 V3 (pines fijos en la placa).
// Module(NSS/CS, DIO1, NRST, BUSY) - el SPI usa SCK 9 / MISO 11 / MOSI 10.
SX1262 radio = new Module(SS, DIO0, RST_LoRa, BUSY_LoRa);

// Nodo LoRaWAN
LoRaWANNode node(&radio, &Region, subBand);

// ===========================================================================
//  3. AYUDAS DE DEPURACION
// ===========================================================================
String stateDecode(const int16_t result) {
  switch (result) {
    case RADIOLIB_ERR_NONE:                 return "ERR_NONE";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:       return "ERR_CHIP_NOT_FOUND (radio no detectada)";
    case RADIOLIB_ERR_PACKET_TOO_LONG:      return "ERR_PACKET_TOO_LONG";
    case RADIOLIB_ERR_MIC_MISMATCH:         return "ERR_MIC_MISMATCH (keys mal copiadas)";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED:   return "ERR_NETWORK_NOT_JOINED";
    case RADIOLIB_ERR_NO_JOIN_ACCEPT:       return "ERR_NO_JOIN_ACCEPT (sin gateway o DevNonce rechazado)";
    case RADIOLIB_ERR_JOIN_NONCE_INVALID:   return "ERR_JOIN_NONCE_INVALID";
    case RADIOLIB_ERR_UPLINK_UNAVAILABLE:   return "ERR_UPLINK_UNAVAILABLE (duty cycle agotado)";
    case RADIOLIB_ERR_DWELL_TIME_EXCEEDED:  return "ERR_DWELL_TIME_EXCEEDED";
    case RADIOLIB_LORAWAN_NEW_SESSION:      return "NEW_SESSION (join OK)";
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
