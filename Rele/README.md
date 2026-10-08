# Rele Heltec V3 - Fase 1 (control local)

Nodo actuador basado en **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262) que
gobierna un modulo de rele de 4 canales. Esta fase valida el control local
(puerto serie y pulsador) antes de integrar LoRaWAN con ChirpStack en la fase 2.

## Estructura

```
platformio.ini                 entorno PlatformIO (RadioLib fijada para la fase 2)
include/config.h               pines y constantes (unico sitio con numeros)
lib/RelayController/           clase que encapsula el rele (unico digitalWrite)
src/main.cpp                   bucle principal: serie + pulsador + traza
chirpstack/codec.js            codec de payload para la fase 2
docs/borrador_fase2/           copia del borrador LoRaWAN previo a esta fase
```

## Conexionado

```
 Heltec WiFi LoRa 32 V3                Modulo rele 4 canales (activo en bajo)
 +---------------------+               +--------------------------+
 |                 5V  |---------------| VCC                      |
 |                GND  |-------+-------| GND                      |
 |             GPIO 4  |-------|-------| IN1   (canal 1)          |
 |             GPIO 5  |  (reservado)  | IN2   (canal 2, futuro)  |
 |             GPIO 7  |  (reservado)  | IN3   (canal 3, futuro)  |
 |                     |               | IN4   (canal 4, futuro)  |
 |                     |               +--------------------------+
 |             GPIO 6  |----+
 |                     |    |  pulsador (pull-up interno, aun no montado)
 |                     |   [ ]
 |                     |    |
 |                GND  |----+
 +---------------------+
```

- El modulo se alimenta a **5 V** (con 3,3 V muchos modulos no cierran el rele).
  Sus entradas INx aceptan los 3,3 V del ESP32-S3 sin adaptacion.
- La carga se conecta entre **COM** y **NO** del canal: con el rele en OFF el
  circuito esta abierto, con ON se cierra.
- El pulsador va entre GPIO 6 y GND; el pull-up interno hace el resto.

## Pines de la placa

| GPIO      | Uso                                     | Estado                   |
|-----------|-----------------------------------------|--------------------------|
| 4         | Rele canal 1 (IN1)                      | **usado**                |
| 5         | Rele canal 2 (IN2)                      | libre, reservado         |
| 6         | Pulsador                                | **usado**                |
| 7         | Rele canal 3 (IN3)                      | libre, reservado         |
| 8-14      | SX1262 LoRa (NSS, SCK, MOSI, MISO, RST, BUSY, DIO1) | **prohibido** |
| 17, 18, 21| OLED integrado (SDA, SCL, RST)          | **prohibido**            |
| 35        | LED de la placa                         | **prohibido**            |
| 26-37     | Flash / PSRAM del ESP32-S3              | **prohibido**            |

## Comandos por puerto serie (115200 baudios)

Insensibles a mayusculas; aceptan `\n`, `\r` o `\r\n` como fin de linea.

| Comando  | Efecto                                              |
|----------|-----------------------------------------------------|
| `ON`     | Activa el rele                                      |
| `OFF`    | Desactiva el rele                                   |
| `TOGGLE` | Invierte el estado                                  |
| `STATUS` | Estado actual, contador de conmutaciones y uptime   |
| `HELP`   | Lista de comandos                                   |

Cualquier otra entrada responde con `Comando desconocido: "..."`.

## Traza de pruebas

Cada cambio efectivo de estado imprime el origen, el estado nuevo y el
numero de conmutacion acumulado:

```
[SERIE] Rele -> ON (conmutacion #1)
[PULSADOR] Rele -> OFF (conmutacion #2)
[SERIE] Rele ya estaba OFF, sin cambios
Estado: OFF | Conmutaciones: 2 | Tiempo encendido: 84 s | GPIO rele: 4 (activo en bajo)
```

## Decisiones de diseno relevantes para la memoria

- **Arranque seguro**: `RelayController::begin()` escribe el nivel de reposo
  antes de `pinMode(OUTPUT)`, evitando el pulso de unos milisegundos que
  activaria el rele en cada reset con un modulo activo en bajo.
- **Polaridad configurable**: `RELAY_ACTIVE_LOW` en `config.h` invierte la
  logica si el modulo fuese activo en alto, sin tocar el resto del codigo.
- **Un unico punto de actuacion**: solo `RelayController` hace `digitalWrite`.
  En la fase 2 el callback de downlink de ChirpStack llamara a `set()` /
  `toggle()` igual que hoy lo hacen el serie y el pulsador.
- **Sin bloqueos**: `loop()` no usa `delay()`; el antirrebote del pulsador
  (250 ms, por estado estable) y la lectura del serie usan `millis()`.

## Compilar y cargar

```
pio run                # compilar
pio run -t upload      # cargar por USB
pio device monitor     # monitor serie a 115200
```
