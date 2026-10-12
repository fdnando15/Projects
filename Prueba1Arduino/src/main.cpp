#include <Arduino.h>

// Guardamos el momento del último disparo de cada tarea
unsigned long ultimoLatido = 0;
unsigned long ultimoInforme = 0;

int nLatido = 0;
int nInforme = 0;

void imprimirLatido() {
  Serial.println("Latido #" + String(nLatido));
  nLatido++;
}

void imprimirInforme() {
  Serial.println("Informe #" + String(nInforme) + "");
  nInforme++;
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("Sistema iniciado.");
}

void loop() {
  unsigned long ahora = millis();

  if (ahora - ultimoLatido >= 1000) {
    ultimoLatido = ahora;
    imprimirLatido();
  }

  if (ahora - ultimoInforme >= 5000) {
    ultimoInforme = ahora;
    imprimirInforme();
  }
}