// RelayController.cpp - ver intencion de diseno en RelayController.h

#include "RelayController.h"

RelayController::RelayController(uint8_t pin, bool activeLow)
    : pin_(pin), activeLow_(activeLow) {}

int RelayController::levelFor(bool on) const {
  // Con modulo activo en bajo: ON -> LOW, OFF -> HIGH. Con activo en alto,
  // al reves. El XOR evita duplicar ramas para las dos polaridades.
  return (on != activeLow_) ? HIGH : LOW;
}

void RelayController::begin(bool initialOn) {
  // ARRANQUE SEGURO: se escribe el nivel de reposo ANTES de poner el pin como
  // salida. Si se hiciera pinMode(OUTPUT) primero, el pin saldria con el
  // valor por defecto del registro (LOW), que en un modulo activo en bajo
  // significa "rele activado": la carga recibiria un pulso de unos ms en cada
  // reset. En el ESP32 digitalWrite fija el registro de salida aunque el pin
  // aun sea entrada, de modo que al habilitar la salida ya aparece el nivel
  // correcto y el rele no llega a moverse.
  on_ = initialOn;
  digitalWrite(pin_, levelFor(on_));
  pinMode(pin_, OUTPUT);
  switchCount_ = 0;   // el estado inicial no cuenta como conmutacion
}

bool RelayController::set(bool on) {
  if (on == on_) {
    return false;     // orden redundante: no conmuta ni suma al contador
  }
  on_ = on;
  digitalWrite(pin_, levelFor(on_));
  switchCount_++;
  return true;
}

bool RelayController::toggle() {
  set(!on_);
  return on_;
}
