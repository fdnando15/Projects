// RelayController.h - Control de un canal de rele.
//
// INTENCION DE DISENO: esta clase es el UNICO punto del proyecto que hace
// digitalWrite sobre el pin del rele. Cualquier via de mando (puerto serie,
// pulsador y, en la fase 2, el callback de downlink LoRaWAN de ChirpStack)
// debe pasar por set()/toggle(). Asi, anadir LoRaWAN no obliga a tocar nada
// de la logica del rele: solo se anade otro "origen" que llama a esta clase.
//
// La clase no conoce config.h a proposito: recibe pin y polaridad por el
// constructor para poder instanciar un objeto por canal (1 a 4) y para que
// la libreria sea reutilizable en otros proyectos.

#pragma once

#include <Arduino.h>

class RelayController {
public:
  // pin: GPIO conectado a la entrada INx del modulo.
  // activeLow: true si el rele se activa con INx = GND (lo habitual).
  RelayController(uint8_t pin, bool activeLow);

  // Configura el pin y deja el rele en initialOn. Llamar una vez en setup().
  void begin(bool initialOn = false);

  // Fija el estado. Devuelve true solo si el estado ha cambiado realmente,
  // para que el llamador pueda registrar la traza solo en cambios efectivos.
  bool set(bool on);

  // Invierte el estado. Devuelve el estado nuevo.
  bool toggle();

  // true = rele activado (contacto NO cerrado).
  bool state() const { return on_; }

  // Numero de conmutaciones efectivas desde begin().
  uint32_t switchCount() const { return switchCount_; }

  uint8_t pin() const { return pin_; }

private:
  // Traduce estado logico -> nivel electrico segun la polaridad del modulo.
  int levelFor(bool on) const;

  uint8_t  pin_;
  bool     activeLow_;
  bool     on_          = false;
  uint32_t switchCount_ = 0;
};
