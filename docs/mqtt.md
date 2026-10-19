# Formato de los datos MQTT

El STM32 publica por el módulo Wi-Fi FC41D. Los datos de la Pico (ciclo, posiciones, voltajes, temperatura) llegan al STM32 por I2C y salen en el mismo mensaje.

- **Broker por defecto:** `broker.hivemq.com`, puerto 1883 (TCP, sin usuario). Es público, sirve solo para pruebas. Se cambia en `bajo_nivel/stm32/inc/wifi_config.h`.
- **Desde un navegador:** el mismo broker acepta WebSocket en `wss://broker.hivemq.com:8884/mqtt`.
- **Prefijo de topics:** `scara/` (configurable). Para no mezclarse con otros usuarios del broker público, conviene cambiarlo por algo único, p. ej. `scara-simon/`.

## `scara/estado` (cada 500 ms, QoS 0, sin retain)

```json
{
  "v": 1,
  "up": 1234,
  "ciclo": "LISTO",
  "pico_ok": true,
  "loop": 123456,
  "temp": { "pico": 34.5, "stm32": 31.2 },
  "vdda": 3.31,
  "ejes": [
    { "n": "R1", "est": "R", "pos": 0.512,  "u": 3.20, "i": 1.25, "en": true,  "lim": [0, 0], "falla": "ninguna" },
    { "n": "R2", "est": "R", "pos": -0.200, "u": -1.10, "i": 0.40, "en": true,  "lim": [0, 0], "falla": "ninguna" },
    { "n": "P",  "est": "H", "pos": 35.12,  "u": -8.00, "i": 0.90, "en": true,  "lim": [1, 0], "falla": "ninguna" }
  ]
}
```

| Campo | Significado |
|---|---|
| `v` | versión del formato (1) |
| `up` | segundos desde que arrancó el STM32 |
| `ciclo` | en qué parte del programa está la Pico (tabla de abajo) |
| `pico_ok` | `true` si la Pico está hablando con el STM32 (latido I2C < 100 ms) |
| `loop` | contador del lazo de control de la Pico (núcleo 1). Si deja de subir, el lazo está trabado |
| `temp.pico` | temperatura interna del RP2040, °C |
| `temp.stm32` | temperatura interna del STM32, °C |
| `vdda` | voltaje de 3.3 V medido por el STM32 |
| `ejes[].n` | `R1`, `R2`, `P` (en ese orden siempre) |
| `ejes[].est` | estado del eje en la Pico: `D` deshabilitado, `H` home, `R` listo, `F` falla |
| `ejes[].pos` | posición: rad en R1/R2, mm en P |
| `ejes[].u` | voltaje medio que la Pico manda al motor, V (con signo: sentido) |
| `ejes[].i` | corriente del motor medida por el STM32, A |
| `ejes[].en` | `true` si el STM32 tiene el EN del driver encendido |
| `ejes[].lim` | finales de carrera `[home, extremo]`, 1 = presionado |
| `ejes[].falla` | `ninguna`, `sostenida` (> 3 A por 500 ms), `pico` (> 4.5 A por 20 ms), `driver` (el IFX007T reportó sobretemperatura o corto), `enlace` (se perdió la Pico) |

Valores de `ciclo`:

| Valor | Qué está haciendo la Pico |
|---|---|
| `ARRANQUE` | inicializando |
| `ESPERA` | esperando el Enter para empezar el home |
| `HOME_R1`, `HOME_R2`, `HOME_P` | haciendo el home de ese eje |
| `LISTO` | en control, siguiendo los objetivos del PC |
| `QUIETO` | en control, pero el PC dejó de mandar objetivos y los ejes se quedaron quietos |
| `FALLA` | un home falló o el STM32 cortó un eje; hay que reiniciar |
| `SIN_PICO` | lo pone el STM32 cuando no recibe datos de la Pico |

**Temperatura de los drivers:** el IFX007T no entrega su temperatura, solo avisa cuando se sobrecalienta (`falla: "driver"`). Por eso no hay un número de temperatura por driver.

## `scara/evento` (al instante, QoS 1)

Se publica cuando un eje entra en falla o cuando se borra una falla.

```json
{ "up": 1240, "eje": "R1", "falla": "pico", "i": 4.82 }
```

`falla: "ninguna"` significa que la falla de ese eje se borró.

## `scara/est1` (lo publica el PC)

La secuencia de la Estación 1 (`alto_nivel/.../estacion1.py`) publica su estado cada segundo y en cada cambio de fase, con `retain`:

```json
{ "tarjeta": 2, "fase": "BANDEJA", "vacio": 1, "colocadas": 1, "t_ciclo": 12.8, "t_ultimo": 34.5, "ciclos": 3 }
```

`fase` es `PILA`, `BAJAR_PILA`, `VACIO`, `SUBIR_PILA`, `CORREGIR`, `BANDEJA`, `BAJAR_BANDEJA`, `SOLTAR`, `SUBIR_BANDEJA` o `ESPERA_PILA`; `tarjeta` es `null` entre ciclos.
