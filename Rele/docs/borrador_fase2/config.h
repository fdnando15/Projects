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
//
// IMPORTANTE para Clase C: en el Device profile de ChirpStack marca
//   "Class-C" -> "Device supports Class-C"  (pestana Class-C)
// Si no, ChirpStack encolara el downlink pero solo lo enviara tras un uplink.

#define JOIN_EUI  0x0000000000000000

#define DEV_EUI   0x0000000000000000

#define APP_KEY   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

#define NWK_KEY   APP_KEY

// Region: EU868 en Espana. Otras: US915, AU915, AS923, IN865, KR920, CN470
const LoRaWANBand_t Region = EU868;
const uint8_t subBand = 0;              // 0 para EU868; 2 para US915/AU915

// Clase LoRaWAN:
//   true  -> Clase C: la radio escucha siempre, el rele responde en 1-2 s.
//            Consume mas (no apto para bateria), pero es lo logico para un
//            actuador enchufado a la red.
//   false -> Clase A: el downlink solo llega justo despues de cada uplink,
//            asi que la orden tardara hasta uplinkIntervalSeconds en aplicarse.
#define USE_CLASS_C  true

// Cada cuanto el nodo envia su estado (heartbeat). Sirve para que ChirpStack
// vea que esta vivo y, en Clase A, para abrir las ventanas de recepcion.
// Duty cycle 1% en EU868: no bajes de ~60 s en pruebas.
const uint32_t uplinkIntervalSeconds = 60UL;

// Puertos de aplicacion (fPort)
const uint8_t uplinkPort   = 10;   // el nodo -> ChirpStack (estado del rele)
const uint8_t downlinkPort = 10;   // ChirpStack -> el nodo (ordenes)

// ===========================================================================
//  2. HARDWARE
// ===========================================================================
// Modulo rele tipico (1 canal, con optoacoplador):
//   VCC -> 5V (pin "5V"/"VIN" de la Heltec; con 3V3 muchos modulos no cierran)
//   GND -> GND
//   IN  -> GPIO definido en RELAY_PIN (la entrada acepta 3.3 V sin problema)
// La bombilla/LED va en los contactos COM y NO (normalmente abierto):
// con el rele en OFF el circuito esta abierto, con ON se cierra.
#define RELAY_PIN  7

// La mayoria de modulos de rele baratos son ACTIVOS A NIVEL BAJO
// (se activan con IN=GND). Si ves que el rele hace justo lo contrario de lo
// que mandas, cambia este valor.
#define RELAY_ACTIVE_LOW  true

// Estado del rele al arrancar (antes de recibir ninguna orden)
#define RELAY_DEFAULT_ON  false

// Si true, tras un reinicio el rele vuelve al ultimo estado ordenado
// (se guarda en NVS). Si false, arranca siempre en RELAY_DEFAULT_ON.
#define RELAY_RESTORE_STATE  true

// Radio SX1262 de la Heltec WiFi LoRa 32 V3 (pines fijos en la placa).
// Module(NSS/CS, DIO1, NRST, BUSY) - el SPI usa SCK 9 / MISO 11 / MOSI 10.
SX1262 radio = new Module(SS, DIO0, RST_LoRa, BUSY_LoRa);

// Nodo LoRaWAN
LoRaWANNode node(&radio, &Region, subBand);

// ===========================================================================
//  3. PROTOCOLO DE PAYLOAD
// ===========================================================================
// DOWNLINK (ChirpStack -> nodo), fPort = downlinkPort:
//   [0x00]              -> rele OFF
//   [0x01]              -> rele ON
//   [0x02]              -> TOGGLE (invierte el estado actual)
//   [0x03, hi, lo]      -> PULSO: rele ON durante (hi<<8|lo) segundos y luego OFF
//
// UPLINK (nodo -> ChirpStack), fPort = uplinkPort, 3 bytes:
//   [0] estado del rele: 0 = OFF, 1 = ON
//   [1] motivo:  0 = heartbeat periodico, 1 = cambio por orden recibida,
//                2 = fin de pulso, 3 = arranque
//   [2] segundos restantes de pulso (0 si no hay pulso activo, max 255)
enum RelayCmd : uint8_t {
  CMD_OFF    = 0x00,
  CMD_ON     = 0x01,
  CMD_TOGGLE = 0x02,
  CMD_PULSE  = 0x03,
};

enum UplinkReason : uint8_t {
  REASON_HEARTBEAT = 0,
  REASON_COMMAND   = 1,
  REASON_PULSE_END = 2,
  REASON_BOOT      = 3,
};

// ===========================================================================
//  4. AYUDAS DE DEPURACION
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
