// main.cpp - Fase 1: control local del rele (serie + pulsador).
//
// Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
//
// El loop() nunca bloquea: la lectura del puerto serie y el antirrebote del
// pulsador se resuelven con millis() y maquinas de estado sencillas. Esto es
// deliberado: en la fase 2 RadioLib necesitara que loop() gire con fluidez
// para atender los downlinks de Clase C.
//
// Todas las ordenes acaban en RelayController; aqui solo se decide QUIEN
// ordena (origen) y se imprime la traza para el apartado de pruebas.

#include <Arduino.h>

#include "RelayController.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------------
static RelayController relay(RELAY_PIN_CH1, RELAY_ACTIVE_LOW);

// Buffer de linea del puerto serie. Los comandos son cortos; si llega basura
// sin fin de linea se descarta al llenarse para no crecer sin control.
static char   serialLine[32];
static size_t serialLen = 0;

// Antirrebote del pulsador (ver pollButton()).
static int      buttonStable      = HIGH;   // ultimo estado validado
static int      buttonLastReading = HIGH;   // ultima lectura cruda
static uint32_t buttonLastChange  = 0;      // millis() del ultimo cambio crudo

// ---------------------------------------------------------------------------
// Traza y aplicacion de ordenes
// ---------------------------------------------------------------------------
static const char* stateText(bool on) { return on ? "ON" : "OFF"; }

// Punto unico por el que pasan todos los cambios de estado. El origen se
// imprime siempre para poder correlacionar la traza con la accion realizada
// durante las pruebas (y, en la fase 2, con el downlink recibido).
static void applyRelay(bool on, const char* origen) {
  if (relay.set(on)) {
    Serial.printf("[%s] Rele -> %s (conmutacion #%lu)\n",
                  origen, stateText(relay.state()),
                  (unsigned long)relay.switchCount());
  } else {
    Serial.printf("[%s] Rele ya estaba %s, sin cambios\n",
                  origen, stateText(relay.state()));
  }
}

static void printStatus() {
  Serial.printf("Estado: %s | Conmutaciones: %lu | Tiempo encendido: %lu s | GPIO rele: %u (%s)\n",
                stateText(relay.state()),
                (unsigned long)relay.switchCount(),
                (unsigned long)(millis() / 1000UL),
                relay.pin(),
                RELAY_ACTIVE_LOW ? "activo en bajo" : "activo en alto");
}

static void printHelp() {
  Serial.println("Comandos disponibles (sin distinguir mayusculas):");
  Serial.println("  ON      - activa el rele");
  Serial.println("  OFF     - desactiva el rele");
  Serial.println("  TOGGLE  - invierte el estado");
  Serial.println("  STATUS  - estado actual y contador de conmutaciones");
  Serial.println("  HELP    - esta ayuda");
}

// ---------------------------------------------------------------------------
// Via 1: comandos por puerto serie
// ---------------------------------------------------------------------------
static void handleCommand(char* cmd) {
  // Normalizamos a mayusculas para aceptar "on", "On", "ON" indistintamente.
  for (char* p = cmd; *p; ++p) {
    *p = toupper(static_cast<unsigned char>(*p));
  }

  if      (strcmp(cmd, "ON") == 0)     { applyRelay(true, "SERIE"); }
  else if (strcmp(cmd, "OFF") == 0)    { applyRelay(false, "SERIE"); }
  else if (strcmp(cmd, "TOGGLE") == 0) { applyRelay(!relay.state(), "SERIE"); }
  else if (strcmp(cmd, "STATUS") == 0) { printStatus(); }
  else if (strcmp(cmd, "HELP") == 0)   { printHelp(); }
  else {
    Serial.printf("Comando desconocido: \"%s\". Escribe HELP para ver la lista.\n", cmd);
  }
}

// Lee lo que haya disponible sin esperar: se acumula caracter a caracter y
// solo se procesa al recibir fin de linea. Tanto '\r' como '\n' cierran la
// linea, y las lineas vacias se ignoran, de modo que "\r\n" (monitor de
// PlatformIO, PuTTY, Arduino IDE...) no genera un segundo comando vacio.
static void pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\r' || c == '\n') {
      if (serialLen > 0) {
        serialLine[serialLen] = '\0';
        handleCommand(serialLine);
        serialLen = 0;
      }
      continue;
    }

    if (c == ' ' || c == '\t') {
      continue;   // toleramos espacios accidentales alrededor del comando
    }

    if (serialLen < sizeof(serialLine) - 1) {
      serialLine[serialLen++] = c;
    } else {
      // Linea demasiado larga: no puede ser un comando valido. Se vacia el
      // buffer y se marca para seguir descartando hasta el fin de linea,
      // evitando procesar un fragmento como si fuese una orden.
      serialLen = 0;
      Serial.println("Linea demasiado larga, descartada");
      while (Serial.available() > 0 && Serial.peek() != '\n' && Serial.peek() != '\r') {
        Serial.read();
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Via 2: pulsador con antirrebote por software
// ---------------------------------------------------------------------------
// Antirrebote por "estado estable": un cambio en la lectura cruda solo se
// acepta si se mantiene BUTTON_DEBOUNCE_MS sin volver a cambiar. Se prefiere
// a la variante de "ignorar X ms tras el primer flanco" porque esta ultima
// puede confundir el rebote de la SUELTA con una nueva pulsacion. El coste
// es un retardo de 250 ms entre pulsar y ver moverse el rele, irrelevante
// para un actuador de este tipo.
static void pollButton() {
  const int      reading = digitalRead(BUTTON_PIN);
  const uint32_t now     = millis();

  if (reading != buttonLastReading) {
    buttonLastReading = reading;
    buttonLastChange  = now;      // reinicia la ventana de estabilidad
  }

  if ((now - buttonLastChange) >= BUTTON_DEBOUNCE_MS && reading != buttonStable) {
    buttonStable = reading;
    if (buttonStable == LOW) {    // flanco de bajada validado = pulsacion
      applyRelay(!relay.state(), "PULSADOR");
    }
  }
}

// ---------------------------------------------------------------------------
void setup() {
  // El rele se inicializa ANTES que el puerto serie: con USB CDC, Serial
  // puede tardar en estar listo y no queremos que el arranque seguro del
  // rele dependa de ello.
  relay.begin(RELAY_DEFAULT_ON);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  buttonStable = buttonLastReading = digitalRead(BUTTON_PIN);
  buttonLastChange = millis();

  Serial.begin(SERIAL_BAUD);

  Serial.println();
  Serial.println("=== Rele Heltec V3 - Fase 1 (control local) ===");
  Serial.printf("Rele en GPIO %u (%s), pulsador en GPIO %u\n",
                RELAY_PIN_CH1, RELAY_ACTIVE_LOW ? "activo en bajo" : "activo en alto",
                BUTTON_PIN);
  Serial.printf("Estado inicial: %s\n", stateText(relay.state()));
  printHelp();
}

void loop() {
  pollSerial();
  pollButton();
}
