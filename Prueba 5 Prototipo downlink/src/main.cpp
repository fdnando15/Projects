// Downlink ChirpStack -> Heltec V3 -> OLED, en Clase C
// Payload del downlink: entero big-endian de 1 a 4 bytes

#include <Arduino.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <Preferences.h>

#include "credentials.h"

// Pines SX1262
static const int8_t LORA_NSS   = 8;
static const int8_t LORA_DIO1  = 14;
static const int8_t LORA_RST   = 12;
static const int8_t LORA_BUSY  = 13;

// Pines OLED
static const int8_t OLED_SDA   = 17;
static const int8_t OLED_SCL   = 18;
static const int8_t OLED_RST   = 21;
static const int8_t VEXT_PIN   = 36;   // alimentacion OLED, LOW = encendida

static const uint32_t UPLINK_INTERVAL_MS = 300000; // latido cada 5 min
static const uint8_t  FPORT              = 1;

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
LoRaWANNode node(&radio, &EU868);

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

Preferences prefs;

static int32_t  ultimoNumero    = -1;      // -1 = nada recibido aun
static uint32_t uplinksEnviados = 0;
static uint32_t ultimoUplinkMs  = 0;

// ---------------- OLED ----------------
void oledInit() {
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(50);

  u8g2.begin();
  u8g2.setContrast(255);
}

void oledEstado(const char* linea1, const char* linea2 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 12, linea1);
  u8g2.drawStr(0, 26, linea2);
  u8g2.sendBuffer();
}

void oledMostrarNumero() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 10, "ChirpStack -> ESP32");
  u8g2.drawHLine(0, 13, 128);

  char buf[24];
  if (ultimoNumero < 0) {
    u8g2.setFont(u8g2_font_9x15_tr);
    snprintf(buf, sizeof(buf), "Esperando...");
  } else {
    u8g2.setFont(u8g2_font_logisoso28_tn);
    snprintf(buf, sizeof(buf), "%ld", (long)ultimoNumero);
  }
  int ancho = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - ancho) / 2, 48, buf);

  u8g2.setFont(u8g2_font_5x8_tr);
  snprintf(buf, sizeof(buf), "uplinks: %lu", (unsigned long)uplinksEnviados);
  u8g2.drawStr(0, 63, buf);

  u8g2.sendBuffer();
}

// ---------------- Nonces en NVS ----------------
// Si el DevNonce se repite tras reiniciar, ChirpStack rechaza el join
void noncesRestaurar() {
  prefs.begin("lorawan", false);
  uint8_t buf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  size_t n = prefs.getBytes("nonces", buf, sizeof(buf));
  if (n == sizeof(buf)) {
    int16_t st = node.setBufferNonces(buf);
    Serial.printf("[NVS] nonces restaurados (estado %d)\n", st);
  } else {
    Serial.println("[NVS] sin nonces guardados (primer arranque)");
  }
}

void noncesGuardar() {
  const uint8_t* buf = node.getBufferNonces();
  prefs.putBytes("nonces", buf, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  Serial.println("[NVS] nonces guardados");
}

// ---------------- Radio y LoRaWAN ----------------
void radioInit() {
  // TCXO a 1.8 V, el resto lo configura la pila LoRaWAN
  int16_t st = radio.begin(868.0, 125.0, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                           10, 8, 1.8, false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[RADIO] fallo en begin(): %d\n", st);
    oledEstado("Error radio", "ver monitor serie");
    while (true) delay(1000);
  }

  radio.setDio2AsRfSwitch(true);   // conmutador de antena
  Serial.println("[RADIO] SX1262 OK");
}

// Uplink confirmado para que el cambio a Clase C se complete con el ACK
void activarClaseC() {
  node.setClass(RADIOLIB_LORAWAN_CLASS_C);

  uint8_t hola[1] = { 0xC0 };
  for (int i = 1; i <= 5; i++) {
    int16_t st = node.sendReceive(hola, sizeof(hola), FPORT, true);
    if (st > 0) {
      Serial.println("[LORAWAN] Clase C activa (ACK recibido)");
      return;
    }
    Serial.printf("[LORAWAN] sin ACK (estado %d), reintento %d/5\n", st, i);
    delay(5000);
  }
  Serial.println("[LORAWAN] Clase C pendiente de confirmar");
}

void lorawanJoin() {
  node.beginOTAA(LORAWAN_JOIN_EUI, LORAWAN_DEV_EUI, NULL, LORAWAN_APP_KEY);   // LoRaWAN 1.0.x

  noncesRestaurar();

  int intento = 0;
  while (true) {
    intento++;
    char linea[32];
    snprintf(linea, sizeof(linea), "intento %d", intento);
    oledEstado("Join OTAA...", linea);
    Serial.printf("[LORAWAN] Join intento %d\n", intento);

    int16_t st = node.activateOTAA();
    if (st == RADIOLIB_LORAWAN_NEW_SESSION) {
      Serial.println("[LORAWAN] Join OK");
      noncesGuardar();
      oledEstado("Join OK", "activando clase C");
      activarClaseC();
      return;
    }

    Serial.printf("[LORAWAN] Join fallido: %d, reintento en 10 s\n", st);
    delay(10000);
  }
}

// ---------------- Downlink ----------------
// [0xC8] -> 200, [0x01, 0x2C] -> 300
bool decodificarNumero(const uint8_t* data, size_t len, int32_t& out) {
  if (len == 0 || len > 4) return false;
  uint32_t acc = 0;
  for (size_t i = 0; i < len; i++) acc = (acc << 8) | data[i];
  out = (int32_t)acc;
  return true;
}

void procesarDownlink(const uint8_t* data, size_t len, const LoRaWANEvent_t& ev) {
  Serial.printf("[LORAWAN] downlink fPort=%u fCnt=%lu datos=", ev.fPort, (unsigned long)ev.fCnt);
  for (size_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
  Serial.println();

  int32_t n;
  if (decodificarNumero(data, len, n)) {
    ultimoNumero = n;
    Serial.printf("[APP] nuevo numero: %ld\n", (long)n);
  } else {
    Serial.println("[APP] payload no reconocido como numero");
  }
  oledMostrarNumero();
}

void comprobarDownlinkClaseC() {
  uint8_t down[64];
  size_t  downLen = 0;
  LoRaWANEvent_t evDown;

  int16_t st = node.getDownlinkClassC(down, &downLen, &evDown);
  if (st > 0 && downLen > 0) {
    procesarDownlink(down, downLen, evDown);
  }
}

// Latido: envia el numero mostrado (0xFFFF si no hay)
void enviarLatido() {
  uint16_t valor = (ultimoNumero < 0) ? 0xFFFF : (uint16_t)ultimoNumero;
  uint8_t up[2] = { (uint8_t)(valor >> 8), (uint8_t)(valor & 0xFF) };

  uint8_t down[64];
  size_t  downLen = 0;
  LoRaWANEvent_t evDown;

  int16_t st = node.sendReceive(up, sizeof(up), FPORT, down, &downLen,
                                false, NULL, &evDown);

  if (st < RADIOLIB_ERR_NONE) {
    Serial.printf("[LORAWAN] sendReceive error: %d\n", st);
    return;
  }

  uplinksEnviados++;
  Serial.printf("[LORAWAN] uplink #%lu enviado, ventana=%d, downLen=%u\n",
                (unsigned long)uplinksEnviados, st, (unsigned)downLen);

  if (st > 0 && downLen > 0) {
    procesarDownlink(down, downLen, evDown);   // downlink en RX1/RX2
  } else {
    oledMostrarNumero();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Heltec V3 + RadioLib LoRaWAN + ChirpStack ===");

  oledInit();
  oledEstado("Iniciando radio...");
  radioInit();
  lorawanJoin();

  oledMostrarNumero();
  ultimoUplinkMs = millis();
}

void loop() {
  comprobarDownlinkClaseC();

  if (millis() - ultimoUplinkMs >= UPLINK_INTERVAL_MS) {
    ultimoUplinkMs = millis();
    enviarLatido();
  }
  delay(10);
}
