/*
  Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
  Modulo rele controlado por downlink LoRaWAN desde ChirpStack

  Flujo:
    1. Join OTAA (o restaura la sesion guardada en NVS).
    2. Si USE_CLASS_C, cambia a Clase C: la radio queda escuchando de forma
       continua y cualquier downlink encolado en ChirpStack llega al momento.
    3. Cada uplinkIntervalSeconds envia un heartbeat con el estado del rele.
    4. Cada vez que cambia el rele por una orden, envia un uplink inmediato
       para que en ChirpStack se vea confirmado el nuevo estado.

  El protocolo de bytes esta descrito en include/config.h.
*/

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

Preferences store;

// Estado del rele
static bool     relayOn        = RELAY_DEFAULT_ON;
static uint32_t pulseEndMillis = 0;      // 0 = sin pulso activo
static bool     pulseActive    = false;

// Temporizacion de uplinks
static uint32_t lastUplinkMillis  = 0;
static bool     uplinkPending     = false;      // hay que enviar ya (cambio de estado)
static uint8_t  pendingReason     = REASON_BOOT;

// ---------------------------------------------------------------------------
// Rele
// ---------------------------------------------------------------------------
static void relayWrite(bool on) {
  relayOn = on;
  digitalWrite(RELAY_PIN, (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
  Serial.print(F("Rele -> "));
  Serial.println(on ? F("ON") : F("OFF"));
#if RELAY_RESTORE_STATE
  store.putBool("relay", on);
#endif
}

static void relayInit() {
  pinMode(RELAY_PIN, OUTPUT);
  bool initial = RELAY_DEFAULT_ON;
#if RELAY_RESTORE_STATE
  initial = store.getBool("relay", RELAY_DEFAULT_ON);
#endif
  relayWrite(initial);
}

static uint8_t pulseRemainingSeconds() {
  if (!pulseActive) { return 0; }
  int32_t ms = (int32_t)(pulseEndMillis - millis());
  if (ms <= 0) { return 0; }
  uint32_t s = (ms + 999) / 1000;
  return s > 255 ? 255 : (uint8_t)s;
}

// Da de baja el pulso si ha vencido. Devuelve true si acaba de terminar.
static bool pulseService() {
  if (!pulseActive) { return false; }
  if ((int32_t)(millis() - pulseEndMillis) < 0) { return false; }
  pulseActive = false;
  Serial.println(F("Fin del pulso"));
  relayWrite(false);
  return true;
}

// ---------------------------------------------------------------------------
// Procesado de ordenes recibidas por downlink
// ---------------------------------------------------------------------------
static void handleDownlink(const uint8_t* data, size_t len, uint8_t fPort) {
  Serial.print(F("Downlink fPort="));
  Serial.print(fPort);
  Serial.print(F(" len="));
  Serial.print(len);
  Serial.print(F(" bytes:"));
  for (size_t i = 0; i < len; i++) {
    Serial.print(' ');
    if (data[i] < 0x10) { Serial.print('0'); }
    Serial.print(data[i], HEX);
  }
  Serial.println();

  if (len == 0) {
    Serial.println(F("(downlink sin datos: solo MAC/ACK)"));
    return;
  }
  if (fPort != downlinkPort) {
    Serial.println(F("Puerto no esperado, se ignora"));
    return;
  }

  switch (data[0]) {
    case CMD_OFF:
      pulseActive = false;
      relayWrite(false);
      break;

    case CMD_ON:
      pulseActive = false;
      relayWrite(true);
      break;

    case CMD_TOGGLE:
      pulseActive = false;
      relayWrite(!relayOn);
      break;

    case CMD_PULSE: {
      uint16_t seconds = 1;
      if (len >= 3) {
        seconds = ((uint16_t)data[1] << 8) | data[2];
      } else if (len == 2) {
        seconds = data[1];
      }
      if (seconds == 0) { seconds = 1; }
      Serial.print(F("Pulso de "));
      Serial.print(seconds);
      Serial.println(F(" s"));
      pulseActive    = true;
      pulseEndMillis = millis() + (uint32_t)seconds * 1000UL;
      relayWrite(true);
      break;
    }

    default:
      Serial.print(F("Orden desconocida: 0x"));
      Serial.println(data[0], HEX);
      return;
  }

  // Confirmamos el nuevo estado a ChirpStack con un uplink inmediato
  uplinkPending = true;
  pendingReason = REASON_COMMAND;
}

// ---------------------------------------------------------------------------
// Persistencia LoRaWAN en NVS
// ---------------------------------------------------------------------------
static void saveNonces() {
  uint8_t* buf = node.getBufferNonces();
  store.putBytes("nonces", buf, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
}

static void saveSession() {
  uint8_t* buf = node.getBufferSession();
  store.putBytes("session", buf, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}

// Devuelve true si se ha restaurado una sesion valida (no hace falta join).
static bool restoreSession() {
  uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  uint8_t session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

  if (store.getBytesLength("nonces") != RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
    Serial.println(F("No hay nonces guardadas: primer arranque"));
    return false;
  }
  store.getBytes("nonces", nonces, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  int16_t state = node.setBufferNonces(nonces);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println(F("Nonces guardadas no validas, se descartan"));
    return false;
  }

  if (store.getBytesLength("session") != RADIOLIB_LORAWAN_SESSION_BUF_SIZE) {
    return false;
  }
  store.getBytes("session", session, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  state = node.setBufferSession(session);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println(F("Sesion guardada no valida, se hara join de nuevo"));
    return false;
  }

  state = node.activateOTAA();
  if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
    Serial.println(F("Sesion LoRaWAN restaurada desde flash"));
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Join OTAA con reintentos
// ---------------------------------------------------------------------------
static void joinNetwork() {
  Serial.println(F("Haciendo join OTAA contra ChirpStack..."));
  uint8_t attempt = 0;
  while (true) {
    int16_t state = node.activateOTAA();
    saveNonces();   // la DevNonce se incrementa en CADA intento: guardarla siempre

    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
      Serial.println(F("Join correcto"));
      saveSession();
      return;
    }

    attempt++;
    Serial.print(F("Join fallido (intento "));
    Serial.print(attempt);
    Serial.print(F("): "));
    Serial.println(stateDecode(state));

    // Espera creciente para no saturar el canal ni el duty cycle
    uint32_t wait = min<uint32_t>(30UL * attempt, 300UL);
    Serial.print(F("Reintento en "));
    Serial.print(wait);
    Serial.println(F(" s"));
    delay(wait * 1000UL);
  }
}

// ---------------------------------------------------------------------------
// Uplink de estado. Procesa tambien el downlink que pueda venir en Rx1/Rx2.
// ---------------------------------------------------------------------------
static void sendStatus(uint8_t reason, bool confirmed = false) {
  uint8_t payload[3];
  payload[0] = relayOn ? 1 : 0;
  payload[1] = reason;
  payload[2] = pulseRemainingSeconds();

  Serial.print(F("Enviando uplink de estado (rele="));
  Serial.print(relayOn ? F("ON") : F("OFF"));
  Serial.print(F(", motivo="));
  Serial.print(reason);
  Serial.println(F(")..."));

  uint8_t downData[64];
  size_t  downLen = 0;
  LoRaWANEvent_t downEvent;

  int16_t state = node.sendReceive(payload, sizeof(payload), uplinkPort,
                                   downData, &downLen, confirmed, NULL, &downEvent);

  if (state < RADIOLIB_ERR_NONE) {
    Serial.print(F("Error en el uplink: "));
    Serial.println(stateDecode(state));
    if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
      joinNetwork();
#if USE_CLASS_C
      node.setClass(RADIOLIB_LORAWAN_CLASS_C);
#endif
    }
  } else if (state > 0) {
    Serial.print(F("Downlink recibido en ventana Rx"));
    Serial.println(state);
    handleDownlink(downData, downLen, downEvent.fPort);
  } else {
    Serial.println(F("Uplink enviado (sin downlink)"));
  }

  saveSession();   // guarda el frame counter actualizado
  lastUplinkMillis = millis();
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(3000);                       // margen para abrir el monitor serie
  Serial.println(F("\n=== Nodo RELE <- LoRaWAN (ChirpStack) ==="));

  store.begin("lorawan", false);
  relayInit();

  Serial.println(F("Inicializando SX1262..."));
  int16_t state = radio.begin();
  debug(state != RADIOLIB_ERR_NONE, F("Fallo al iniciar la radio"), state, true);

  uint64_t joinEUI = JOIN_EUI;
  uint64_t devEUI  = DEV_EUI;
  uint8_t appKey[] = { APP_KEY };
  uint8_t nwkKey[] = { NWK_KEY };

  state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  debug(state != RADIOLIB_ERR_NONE, F("Fallo al configurar OTAA"), state, true);

  // Respeta el 1% de duty cycle legal en EU868
  node.setDutyCycle(true, 1250);

  if (!restoreSession()) {
    joinNetwork();
  }

#if USE_CLASS_C
  // Clase C: la radio queda en recepcion continua entre uplinks.
  // RadioLib completa el cambio de clase cuando recibe un downlink; por eso
  // el primer uplink va confirmado (ChirpStack contesta con un ACK).
  Serial.println(F("Cambiando a Clase C..."));
  state = node.setClass(RADIOLIB_LORAWAN_CLASS_C);
  debug(state != RADIOLIB_ERR_NONE, F("No se pudo poner Clase C"), state, false);
  sendStatus(REASON_BOOT, true);
#else
  sendStatus(REASON_BOOT);
#endif

  Serial.println(F("Listo. Encola un downlink en ChirpStack para mover el rele.\n"));
}

// ---------------------------------------------------------------------------
void loop() {
#if USE_CLASS_C
  // Downlinks que llegan en la ventana continua RxC (Clase C).
  // Solo consulta un flag interno; no toca la radio por SPI.
  uint8_t downData[64];
  size_t  downLen = 0;
  LoRaWANEvent_t downEvent;
  int16_t state = node.getDownlinkClassC(downData, &downLen, &downEvent);
  if (state > 0) {
    Serial.println(F("Downlink Clase C recibido"));
    handleDownlink(downData, downLen, downEvent.fPort);
  }
#endif

  // Fin de pulso -> apagar y avisar
  if (pulseService()) {
    uplinkPending = true;
    pendingReason = REASON_PULSE_END;
  }

  // Uplink inmediato por cambio de estado (breve espera para no encadenar
  // dos transmisiones seguidas si llegan varias ordenes a la vez)
  if (uplinkPending && millis() - lastUplinkMillis > 2000UL) {
    uplinkPending = false;
    sendStatus(pendingReason);
    return;
  }

  // Heartbeat periodico
  if (millis() - lastUplinkMillis >= uplinkIntervalSeconds * 1000UL) {
    sendStatus(REASON_HEARTBEAT);
    return;
  }

  delay(10);
}
