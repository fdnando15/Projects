// ============================================================================
//  Heltec WiFi LoRa 32 V3  +  RadioLib (LoRaWAN OTAA)  +  ChirpStack
//
//  Flujo:
//    Frontend -> API ChirpStack (encola un downlink con un número)
//             -> Gateway -> SX1262 de la placa -> pantalla OLED
//
//  El dispositivo trabaja en CLASE C: tras el join deja la radio escuchando
//  de forma continua en la ventana RX2, así que ChirpStack puede entregar el
//  downlink en cuanto se encola, sin esperar a que el nodo transmita.
//  Además se envía un uplink de "latido" cada UPLINK_INTERVAL_MS para que
//  ChirpStack vea el dispositivo activo. El número recibido se guarda y se
//  muestra en la OLED hasta que llegue otro.
//
//  Formato del downlink (debe coincidir con encodeDownlink() en ChirpStack):
//  entero binario big-endian de 1 a 4 bytes. Ej.: 200 -> [0xC8]
// ============================================================================

#include <Arduino.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <Preferences.h>

#include "credentials.h"

// ----------------------------------------------------------------------------
// 1. Pines de la Heltec WiFi LoRa 32 V3
// ----------------------------------------------------------------------------
// SX1262 (radio LoRa) conectado por SPI al ESP32-S3
static const int8_t LORA_NSS   = 8;    // Chip select
static const int8_t LORA_DIO1  = 14;   // Interrupción (TX done / RX done)
static const int8_t LORA_RST   = 12;   // Reset del chip
static const int8_t LORA_BUSY  = 13;   // Señal "ocupado" del SX1262

// OLED SSD1306 128x64 por I2C (bus dedicado, distinto del I2C "normal")
static const int8_t OLED_SDA   = 17;
static const int8_t OLED_SCL   = 18;
static const int8_t OLED_RST   = 21;
static const int8_t VEXT_PIN   = 36;   // Alimentación de la OLED: LOW = encendida

// ----------------------------------------------------------------------------
// 2. Parámetros de la aplicación
// ----------------------------------------------------------------------------
static const uint32_t UPLINK_INTERVAL_MS = 300000; // latido cada 5 min (en Clase C los downlinks llegan solos)
static const uint8_t  FPORT              = 1;      // puerto LoRaWAN usado (1..223)

// ----------------------------------------------------------------------------
// 3. Objetos globales
// ----------------------------------------------------------------------------
// Driver del SX1262. Module(NSS, DIO1, RESET, BUSY)
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

// Nodo LoRaWAN sobre esa radio, banda europea 868 MHz.
// (Otras regiones: US915, AU915, AS923... para US915/AU915 añade subBand = 2)
LoRaWANNode node(&radio, &EU868);

// Pantalla: modo "F" = frame buffer completo en RAM (más cómodo para dibujar)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

// Memoria no volátil (NVS) para guardar los nonces de LoRaWAN entre reinicios
Preferences prefs;

// Estado de la aplicación
static int32_t  ultimoNumero    = -1;      // -1 = todavía no hemos recibido nada
static uint32_t uplinksEnviados = 0;
static uint32_t ultimoUplinkMs  = 0;

// ----------------------------------------------------------------------------
// 4. Pantalla OLED
// ----------------------------------------------------------------------------
void oledInit() {
  // La OLED de la Heltec V3 se alimenta a través de "Vext". Hay que ponerlo a
  // LOW para que reciba corriente; si no, la pantalla no responde.
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(50);

  u8g2.begin();
  u8g2.setContrast(255);
}

// Muestra un mensaje de estado (dos líneas pequeñas). Se usa durante el
// arranque y el join.
void oledEstado(const char* linea1, const char* linea2 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 12, linea1);
  u8g2.drawStr(0, 26, linea2);
  u8g2.sendBuffer();
}

// Pantalla principal: número grande en el centro + info pequeña abajo.
void oledMostrarNumero() {
  u8g2.clearBuffer();

  // Cabecera
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 10, "ChirpStack -> ESP32");
  u8g2.drawHLine(0, 13, 128);

  // Número grande (o texto de espera si aún no ha llegado ninguno)
  char buf[24];
  if (ultimoNumero < 0) {
    u8g2.setFont(u8g2_font_9x15_tr);
    snprintf(buf, sizeof(buf), "Esperando...");
  } else {
    u8g2.setFont(u8g2_font_logisoso28_tn);   // fuente grande, solo dígitos
    snprintf(buf, sizeof(buf), "%ld", (long)ultimoNumero);
  }
  // Centrar horizontalmente
  int ancho = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - ancho) / 2, 48, buf);

  // Pie: contador de uplinks para ver que "sigue vivo"
  u8g2.setFont(u8g2_font_5x8_tr);
  snprintf(buf, sizeof(buf), "uplinks: %lu", (unsigned long)uplinksEnviados);
  u8g2.drawStr(0, 63, buf);

  u8g2.sendBuffer();
}

// ----------------------------------------------------------------------------
// 5. Persistencia de nonces (evita que ChirpStack rechace el join tras reiniciar)
// ----------------------------------------------------------------------------
// En OTAA cada Join Request lleva un DevNonce que NO puede repetirse. RadioLib
// lo lleva como contador; si tras un reinicio empezara otra vez desde 0,
// ChirpStack respondería "DevNonce already used" y nunca haríamos join.
// Guardamos el buffer de nonces en la flash (NVS) y lo restauramos al arrancar.
void noncesRestaurar() {
  prefs.begin("lorawan", /*readOnly=*/false);
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

// ----------------------------------------------------------------------------
// 6. Inicialización de la radio, join OTAA y paso a Clase C
// ----------------------------------------------------------------------------
void radioInit() {
  // begin(freq, bw, sf, cr, syncWord, power, preamble, tcxoVoltage, useLDO)
  // Sólo importan los dos últimos: la Heltec V3 lleva un TCXO alimentado por
  // DIO3 a 1.8 V. El resto de parámetros los sobreescribe la pila LoRaWAN.
  int16_t st = radio.begin(868.0, 125.0, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                           10, 8, /*tcxoVoltage=*/1.8, /*useRegulatorLDO=*/false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[RADIO] fallo en begin(): %d\n", st);
    oledEstado("Error radio", "ver monitor serie");
    while (true) delay(1000);
  }

  // En la Heltec V3 el conmutador de antena (TX/RX) lo controla el pin DIO2
  // del propio SX1262. Sin esto transmite, pero la antena no está conectada.
  radio.setDio2AsRfSwitch(true);
  Serial.println("[RADIO] SX1262 OK");
}

// Cambia el nodo a Clase C. Según RadioLib, el cambio se completa cuando se
// recibe un downlink; lo forzamos enviando un uplink CONFIRMADO, al que
// ChirpStack responde siempre con un ACK. Después de eso la radio queda
// escuchando permanentemente en RX2 (869.525 MHz, SF12 en EU868).
void activarClaseC() {
  node.setClass(RADIOLIB_LORAWAN_CLASS_C);

  uint8_t hola[1] = { 0xC0 };
  for (int i = 1; i <= 5; i++) {
    int16_t st = node.sendReceive(hola, sizeof(hola), FPORT, /*isConfirmed=*/true);
    if (st > 0) {
      Serial.println("[LORAWAN] Clase C activa (ACK recibido)");
      return;
    }
    Serial.printf("[LORAWAN] sin ACK (estado %d), reintento %d/5\n", st, i);
    delay(5000);
  }
  // Si no hubo ACK seguimos igualmente: el siguiente uplink lo completará.
  Serial.println("[LORAWAN] Clase C pendiente de confirmar");
}

void lorawanJoin() {
  // Credenciales. nwkKey = NULL => LoRaWAN 1.0.x (perfil típico en ChirpStack).
  // Para 1.1.x: node.beginOTAA(JOIN_EUI, DEV_EUI, LORAWAN_NWK_KEY, LORAWAN_APP_KEY)
  node.beginOTAA(LORAWAN_JOIN_EUI, LORAWAN_DEV_EUI, /*nwkKey=*/NULL, LORAWAN_APP_KEY);

  noncesRestaurar();

  // Reintenta el join hasta conseguirlo. activateOTAA() envía el Join Request,
  // espera el Join Accept y configura canales, DevAddr, claves de sesión...
  int intento = 0;
  while (true) {
    intento++;
    char linea[32];
    snprintf(linea, sizeof(linea), "intento %d", intento);
    oledEstado("Join OTAA...", linea);
    Serial.printf("[LORAWAN] Join intento %d\n", intento);

    int16_t st = node.activateOTAA();
    // RADIOLIB_LORAWAN_NEW_SESSION es el valor "bueno": sesión nueva creada.
    if (st == RADIOLIB_LORAWAN_NEW_SESSION) {
      Serial.println("[LORAWAN] Join OK");
      noncesGuardar();          // ya hemos consumido un DevNonce: persistirlo
      oledEstado("Join OK", "activando clase C");
      activarClaseC();
      return;
    }

    Serial.printf("[LORAWAN] Join fallido: %d, reintento en 10 s\n", st);
    delay(10000);
  }
}

// ----------------------------------------------------------------------------
// 7. Decodificación del downlink
// ----------------------------------------------------------------------------
// El codec de ChirpStack (encodeDownlink) convierte { "valor": N } en bytes
// binarios big-endian. Aquí hacemos la operación inversa: 1..4 bytes -> entero.
// Ej.: [0xC8] -> 200 ; [0x01, 0x2C] -> 300
bool decodificarNumero(const uint8_t* data, size_t len, int32_t& out) {
  if (len == 0 || len > 4) return false;
  uint32_t acc = 0;
  for (size_t i = 0; i < len; i++) acc = (acc << 8) | data[i];
  out = (int32_t)acc;
  return true;
}

// Procesa un downlink ya recibido (sirve tanto para los de Clase C como para
// los que llegan en RX1/RX2 tras un uplink).
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

// ----------------------------------------------------------------------------
// 8. Recepción Clase C y uplink de latido
// ----------------------------------------------------------------------------
// Comprueba si la radio ha captado un downlink de Clase C (RX2 continuo).
// getDownlinkClassC() sólo mira un flag interno: es muy barato llamarla
// en cada vuelta del loop.
void comprobarDownlinkClaseC() {
  uint8_t down[64];
  size_t  downLen = 0;
  LoRaWANEvent_t evDown;

  int16_t st = node.getDownlinkClassC(down, &downLen, &evDown);
  if (st > 0 && downLen > 0) {
    procesarDownlink(down, downLen, evDown);
  }
}

void enviarLatido() {
  // Payload de subida: el número que estamos mostrando (2 bytes big-endian).
  // Es sólo informativo (latido): en Clase C los downlinks no dependen de él.
  uint16_t valor = (ultimoNumero < 0) ? 0xFFFF : (uint16_t)ultimoNumero;
  uint8_t up[2] = { (uint8_t)(valor >> 8), (uint8_t)(valor & 0xFF) };

  uint8_t down[64];
  size_t  downLen = 0;
  LoRaWANEvent_t evDown;

  // sendReceive() transmite, espera RX1 y RX2, y devuelve:
  //   >0  ventana en la que llegó el downlink (1 = RX1, 2 = RX2)
  //    0  uplink enviado, sin downlink
  //   <0  error (ver RADIOLIB_ERR_*)
  // Al terminar, RadioLib vuelve sola a la escucha continua de Clase C.
  int16_t st = node.sendReceive(up, sizeof(up), FPORT, down, &downLen,
                                /*isConfirmed=*/false, /*eventUp=*/NULL, &evDown);

  if (st < RADIOLIB_ERR_NONE) {
    Serial.printf("[LORAWAN] sendReceive error: %d\n", st);
    return;
  }

  uplinksEnviados++;
  Serial.printf("[LORAWAN] uplink #%lu enviado, ventana=%d, downLen=%u\n",
                (unsigned long)uplinksEnviados, st, (unsigned)downLen);

  if (st > 0 && downLen > 0) {
    // ChirpStack aprovechó la ventana RX1/RX2 para mandar un downlink
    procesarDownlink(down, downLen, evDown);
  } else {
    oledMostrarNumero();   // refrescar el contador de uplinks
  }
}

// ----------------------------------------------------------------------------
// 9. setup() / loop()
// ----------------------------------------------------------------------------
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
  // 1) ¿Ha llegado algo por la escucha continua de Clase C? (cada vuelta)
  comprobarDownlinkClaseC();

  // 2) Latido periódico. Sin delay() bloqueante largo, para poder añadir
  //    botones, sensores, etc. sin parar el loop.
  if (millis() - ultimoUplinkMs >= UPLINK_INTERVAL_MS) {
    ultimoUplinkMs = millis();
    enviarLatido();
  }
  delay(10);
}
