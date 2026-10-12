#include <Arduino.h>
#include <RadioLib.h>
#include <Preferences.h>

// Heltec WiFi LoRa 32 V4 (pinout compatible con V3) — SX1262
SX1262 radio = new Module(8, 14, 12, 13);  // NSS, DIO1, RST, BUSY
LoRaWANNode node(&radio, &EU868);

Preferences store;

// Configuración chiroptack
uint64_t joinEUI = 0x0000000000000000;
uint64_t devEUI  = 0xE1C0D773FF5B7E7B;

uint8_t appKey[] = {
  0x4F, 0x0B, 0xA4, 0x3D, 0xD2, 0xD6, 0xF3, 0xD2,
  0xFC, 0x79, 0x86, 0x55, 0xE4, 0xC9, 0x19, 0x94
};

// Tratamiento devnonces
void guardarNonces() {
  uint8_t *nonces = node.getBufferNonces();
  store.putBytes("nonces", nonces, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
}

void guardarSesion() {
  uint8_t *session = node.getBufferSession();
  store.putBytes("session", session, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // margen para que el USB CDC enumere

  Serial.println("\n=== Nodo LoRaWAN - TFG ===");

  Serial.println("Iniciando SX1262...");
  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("Error al iniciar radio: %d\n", state);
    while (true);
  }

  // 14 dBm, maximo en EU868
  radio.setOutputPower(14);

  node.beginOTAA(joinEUI, devEUI, NULL, appKey);

  store.begin("lorawan", false);

  // Recuperar DevNonce de la NVS para no repetirlo tras un reinicio
  if (store.isKey("nonces")) {
    uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    store.getBytes("nonces", buffer, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    node.setBufferNonces(buffer);
    Serial.println("Nonces recuperados de NVS");
  } else {
    Serial.println("Sin nonces previos (primer arranque)");
  }

  // Intentar restaurar la sesion anterior y evitar un join nuevo
  bool sesionRestaurada = false;
  if (store.isKey("session")) {
    uint8_t buffer[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
    store.getBytes("session", buffer, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
    if (node.setBufferSession(buffer) == RADIOLIB_ERR_NONE) {
      if (node.activateOTAA() == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        Serial.println("Sesion restaurada, no hace falta join");
        sesionRestaurada = true;
      }
    }
  }

  if (!sesionRestaurada) {
    Serial.println("Uniendose a la red (OTAA)...");
    state = node.activateOTAA();

    guardarNonces();  // guardar SIEMPRE, incluso si falla

    if (state != RADIOLIB_LORAWAN_NEW_SESSION) {
      Serial.printf("Error en Join: %d\n", state);
      Serial.println("Reintentando en 60 s...");
      delay(60000);
      ESP.restart();
    }

    Serial.println("Unido con exito!");
    guardarSesion();
  }
}

void loop() {
  uint8_t payload[] = { 42 };
  Serial.println("Enviando: 42");
  int state = node.sendReceive(payload, sizeof(payload), 1);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Enviado (sin downlink)");
  } else if (state > 0) {
    Serial.printf("Enviado, downlink recibido en RX%d\n", state);
  } else {
    Serial.printf("Error al enviar: %d\n", state);
  }

  guardarSesion();  // los contadores de trama cambian en cada envio

  // Duty cycle EU868: 1% por subbanda
  delay(60000);
}