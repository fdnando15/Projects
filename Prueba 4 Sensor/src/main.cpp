// DHT11 -> LoRaWAN OTAA -> ChirpStack
// Payload: temperatura int16 x100, humedad uint16 x100 (big-endian)

#include <Arduino.h>
#include <Preferences.h>
#include <DHT.h>
#include "config.h"

DHT dht(DHT_PIN, DHT_TYPE);
Preferences store;

static void saveNonces() {
  uint8_t* buf = node.getBufferNonces();
  store.putBytes("nonces", buf, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
}

static void saveSession() {
  uint8_t* buf = node.getBufferSession();
  store.putBytes("session", buf, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}

// Fija el DevNonce inicial para no repetir los que ChirpStack ya conoce
static void seedDevNonce() {
  uint8_t* buf = node.getBufferNonces();
  buf[RADIOLIB_LORAWAN_NONCES_DEV_NONCE]     = highByte(devNonceStart);
  buf[RADIOLIB_LORAWAN_NONCES_DEV_NONCE + 1] = lowByte(devNonceStart);
  buf = node.getBufferNonces();            // recalcula la firma

  uint8_t copy[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  memcpy(copy, buf, sizeof(copy));
  node.setBufferNonces(copy);
  Serial.print(F("DevNonce inicial fijado en "));
  Serial.println(devNonceStart);
}

// true si se restaura la sesion de NVS y no hace falta join
static bool restoreSession() {
  uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  uint8_t session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

  if (store.getBytesLength("nonces") != RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
    Serial.println(F("No hay nonces guardadas: primer arranque"));
    seedDevNonce();
    return false;
  }
  store.getBytes("nonces", nonces, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  int16_t state = node.setBufferNonces(nonces);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println(F("Nonces guardadas no validas, se descartan"));
    seedDevNonce();
    return false;
  }

  uint16_t stored = ((uint16_t)nonces[RADIOLIB_LORAWAN_NONCES_DEV_NONCE] << 8) |
                    nonces[RADIOLIB_LORAWAN_NONCES_DEV_NONCE + 1];
  if (stored < devNonceStart) {
    seedDevNonce();
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

static void joinNetwork() {
  Serial.println(F("Haciendo join OTAA contra ChirpStack..."));
  uint8_t attempt = 0;
  while (true) {
    int16_t state = node.activateOTAA();
    saveNonces();   // el DevNonce sube en cada intento

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

    uint32_t wait = min<uint32_t>(30UL * attempt, 300UL);
    Serial.print(F("Reintento en "));
    Serial.print(wait);
    Serial.println(F(" s"));
    delay(wait * 1000UL);
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println(F("\n=== Nodo DHT11 -> LoRaWAN ==="));

  // Comprobacion del cableado: en reposo DATA debe estar en HIGH
  pinMode(DHT_PIN, INPUT_PULLUP);
  delay(100);
  Serial.print(F("Nivel en reposo de DATA (GPIO "));
  Serial.print(DHT_PIN);
  Serial.print(F("): "));
  Serial.println(digitalRead(DHT_PIN) ? F("HIGH (ok)") : F("LOW (revisa cableado)"));

  // Senal de inicio y conteo de flancos de respuesta (0 = no hay sensor)
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(20);
  pinMode(DHT_PIN, INPUT_PULLUP);
  {
    uint32_t t0 = micros();
    int last = digitalRead(DHT_PIN);
    uint16_t edges = 0;
    while (micros() - t0 < 6000) {
      int now = digitalRead(DHT_PIN);
      if (now != last) { edges++; last = now; }
    }
    Serial.print(F("Sonda DHT: "));
    Serial.print(edges);
    Serial.println(F(" flancos tras la senal de inicio"));
  }
  delay(2000);

  dht.begin();

  Serial.println(F("Inicializando SX1262..."));
  int16_t state = radio.begin();
  debug(state != RADIOLIB_ERR_NONE, F("Fallo al iniciar la radio"), state, true);

  uint64_t joinEUI = JOIN_EUI;
  uint64_t devEUI  = DEV_EUI;
  uint8_t appKey[] = { APP_KEY };

  state = node.beginOTAA(joinEUI, devEUI, NULL, appKey);   // LoRaWAN 1.0.x
  debug(state != RADIOLIB_ERR_NONE, F("Fallo al configurar OTAA"), state, true);

  node.setDutyCycle(true, 1250);

  store.begin("lorawan", false);
  if (!restoreSession()) {
    joinNetwork();
  }

  Serial.println(F("Listo\n"));
}

void loop() {
  float humidity    = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println(F("Lectura del DHT11 fallida"));
    delay(2500);
    return;
  }

  Serial.print(F("T = "));
  Serial.print(temperature, 1);
  Serial.print(F(" C   H = "));
  Serial.print(humidity, 1);
  Serial.println(F(" %"));

  int16_t  tempCenti = (int16_t)lroundf(temperature * 100.0f);
  uint16_t humCenti  = (uint16_t)lroundf(humidity * 100.0f);

  uint8_t payload[4];
  payload[0] = highByte(tempCenti);
  payload[1] = lowByte(tempCenti);
  payload[2] = highByte(humCenti);
  payload[3] = lowByte(humCenti);

  Serial.println(F("Enviando uplink..."));
  int16_t state = node.sendReceive(payload, sizeof(payload), uplinkPort);

  if (state < RADIOLIB_ERR_NONE) {
    Serial.print(F("Error en el uplink: "));
    Serial.println(stateDecode(state));
    if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
      joinNetwork();
    }
  } else if (state > 0) {
    Serial.print(F("Downlink recibido en ventana Rx"));
    Serial.println(state);
  } else {
    Serial.println(F("Uplink enviado (sin downlink)"));
  }

  saveSession();   // guarda el contador de tramas

  Serial.print(F("Siguiente envio en "));
  Serial.print(uplinkIntervalSeconds);
  Serial.println(F(" s\n"));
  delay(uplinkIntervalSeconds * 1000UL);
}
