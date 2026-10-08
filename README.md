# Proyectos PlatformIO

Pruebas de firmware realizadas para el TFG, ordenadas según se fueron haciendo.
Cada prueba parte de lo aprendido en la anterior: primero multitarea en un
microcontrolador, después un sistema operativo de tiempo real y, a partir de
ahí, comunicación LoRaWAN con un servidor ChirpStack, primero enviando datos
(uplink) y después recibiendo órdenes (downlink).

| Carpeta | Placa | Framework | Fecha | Objetivo |
|---|---|---|---|---|
| `Prueba 1 Arduino` | Arduino Uno | Arduino | abr 2026 | Multitarea sin bloqueos con `millis()` |
| `Prueba 2 RTOS` | Heltec WiFi LoRa 32 V3 | ESP-IDF (FreeRTOS) | may 2026 | La misma multitarea con tareas de FreeRTOS |
| `Prueba 3 Prototipo uplink` | Heltec WiFi LoRa 32 V3 | Arduino + RadioLib | sep 2026 | Primer join OTAA y envío de datos a ChirpStack |
| `Prueba 4 Sensor` | Heltec WiFi LoRa 32 V3 + DHT11 | Arduino + RadioLib | sep 2026 | Envío de temperatura y humedad reales |
| `Prueba 5 Prototipo downlink` | Heltec WiFi LoRa 32 V3 (OLED) | Arduino + RadioLib + U8g2 | sep 2026 | Recepción de datos desde ChirpStack en Clase C |
| `Rele` | Heltec WiFi LoRa 32 V3 + módulo de relé | Arduino | sep 2026 | Actuador: control de un relé (en desarrollo) |

La Heltec WiFi LoRa 32 V3 lleva un ESP32-S3 y un transceptor LoRa SX1262.
Todas las pruebas LoRaWAN trabajan en la banda europea EU868.

---

## Prueba 1 Arduino

Dos tareas periódicas ejecutadas "a la vez" en un Arduino Uno, sin usar
`delay()`. En cada vuelta del `loop()` se compara `millis()` con el instante
de la última ejecución de cada tarea:

- cada 1 s imprime `Latido #N`;
- cada 5 s imprime `--- Informe #N ---`.

Es multitarea cooperativa: el bucle no se bloquea nunca, así que una tarea
no retrasa a la otra. Salida por puerto serie a 9600 baudios.

## Prueba 2 RTOS

El mismo ejercicio que la prueba 1, portado al ESP32-S3 con ESP-IDF. Cada
tarea es ahora una tarea de **FreeRTOS** (`xTaskCreate`, 2 KB de pila,
prioridad 5) que espera con `vTaskDelay`. No existe `loop()`: es el
planificador del sistema operativo quien reparte el tiempo de CPU.

Los mensajes se envían con `ESP_LOGI` a 115200 baudios.

> ESP-IDF no admite espacios en la ruta del proyecto. Si la compilación
> falla, renombrar la carpeta sin espacios (por ejemplo `Prueba_2_RTOS`).

## Prueba 3 Prototipo uplink

Primer nodo **LoRaWAN** conectado a ChirpStack mediante la librería RadioLib.

- Activación **OTAA** con las credenciales del dispositivo (DevEUI, JoinEUI y
  AppKey) dadas de alta en ChirpStack.
- Potencia de emisión de 14 dBm, el límite legal en EU868.
- Los **nonces** y la **sesión** se guardan en la memoria no volátil (NVS) del
  ESP32. Así, tras un reinicio, el nodo recupera la sesión sin repetir el join
  y sin reutilizar un DevNonce, que ChirpStack rechazaría.
- Si el join falla, espera 60 s y reinicia la placa.
- Cada 60 s envía un uplink fijo de un byte (`42`) por el puerto 1 y guarda
  la sesión, ya que los contadores de trama cambian en cada envío.

## Prueba 4 Sensor

Nodo LoRaWAN que mide temperatura y humedad con un **DHT11** (GPIO 4) y las
envía a ChirpStack cada 60 s por el puerto 10.

**Formato del payload** (4 bytes, big-endian):

| Bytes | Tipo | Contenido |
|---|---|---|
| 0–1 | `int16` | Temperatura en centésimas de °C (2350 = 23,50 °C) |
| 2–3 | `uint16` | Humedad en centésimas de % (4520 = 45,20 %) |

`chirpstack/codec.js` es el decodificador que se carga en el perfil del
dispositivo en ChirpStack. Convierte esos bytes en los campos `temperature`
y `humidity`, respetando el signo de las temperaturas negativas.

Mejoras respecto a la prueba 3:

- configuración separada del código en `include/config.h`;
- DevNonce inicial configurable, para no chocar con los que ChirpStack ya
  tiene registrados de pruebas anteriores;
- validación de los datos leídos de NVS antes de usarlos;
- reintentos de join con espera creciente (de 30 s a un máximo de 300 s);
- control del duty cycle del 1 % exigido en EU868;
- nuevo join automático si el nodo pierde la red;
- traducción de los códigos de error de RadioLib a texto.

Al arrancar hace además un diagnóstico del cableado del sensor: lee el nivel
de reposo de la línea DATA y cuenta los flancos con los que responde el DHT11.

## Prueba 5 Prototipo downlink

Prueba del camino inverso: un número enviado desde un frontend llega a la
placa y se muestra en su **pantalla OLED**.

```
Frontend -> API de ChirpStack -> Gateway -> SX1262 -> OLED
```

- Join OTAA con reintentos cada 10 s. Se guardan los nonces en NVS; la sesión
  no, por lo que se hace un join nuevo en cada arranque.
- Tras el join el nodo pasa a **Clase C**: envía un uplink confirmado y,
  recibido el ACK, deja la radio escuchando de forma continua. Así ChirpStack
  puede entregar un downlink en cuanto se encola, sin esperar a un uplink.
- En cada vuelta del `loop()` se comprueba si ha llegado un downlink. El
  payload es un entero big-endian de 1 a 4 bytes, que se muestra en grande
  en la OLED.
- Cada 5 min envía un uplink de latido (2 bytes con el número mostrado) por
  el puerto 1, para que ChirpStack vea el dispositivo activo.

Las credenciales están en `include/credentials.h`.

## Rele

Nodo actuador que controla un módulo de relé de 4 canales, del que de momento
solo se usa el canal 1 (GPIO 4, activo en bajo). Está en su **fase 1: control
local**, sin LoRaWAN:

- órdenes por puerto serie: `ON`, `OFF`, `TOGGLE`, `STATUS`, `HELP`;
- pulsador en GPIO 6 con antirrebote por software (250 ms).

La clase `RelayController` (`lib/RelayController/`) es el único punto que
actúa sobre el relé. Arranca en estado seguro, sin pulsos al reiniciar, y
cuenta las conmutaciones.

Para la fase 2 ya están preparados el codec de ChirpStack con órdenes
ON/OFF/TOGGLE/PULSO (`chirpstack/codec.js`) y un borrador del firmware en
Clase C (`docs/borrador_fase2/`). Más detalle en [`Rele/README.md`](Rele/README.md).

---

## Compilar y cargar

Cada carpeta es un proyecto PlatformIO independiente. Desde la carpeta del
proyecto:

```
pio run                # compilar
pio run -t upload      # cargar en la placa
pio device monitor     # monitor serie
```
