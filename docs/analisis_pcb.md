# Adaptación del firmware SCARA a la PCB nueva

Fuente: esquemático `Schematic_PDF_No_Variations.pdf` (3 hojas, Altium) y repo `jer0valencia/SCARA_RRP_`.
Lo marcado como **(supuesto)** es inferido y hay que confirmarlo.

## 1. Qué cambia en la arquitectura

| | Baquelita | PCB nueva |
|---|---|---|
| Control de ejes | Pico | Pico (igual) |
| Enables de los drivers | Pico, 2 EN por motor | **STM32F030**, 1 EN por motor (PB12/PB13/PB14, pull-down 10k: arrancan apagados) |
| Corriente de los motores | no se mide | STM32 lee 6 ADC (PA0–PA5), 2 por motor **(supuesto: IS_L/IS_R de un BTS7960)** |
| Encoders | directo a la Pico | pasan por un SN74LVC8T245 (5 V → 3.3 V) |
| Finales de carrera | 3 | 6 (LIMIT1–LIMIT6), activos en bajo, filtro RC 1k/0.1 µF |
| Extra | — | SERVO_PWM, PUMP_PWM, I2C Pico↔STM32, Wi-Fi FC41D por UART |

## 2. Pines de la Pico

| Señal | Baquelita (código actual) | PCB |
|---|---|---|
| M1 (R1) PWM A/B | GP6 / GP7 | GP0 / GP1 |
| M2 (R2) PWM A/B | GP14 / GP15 | GP2 / GP3 |
| M3 (P) PWM A/B | GP1 / GP0 | GP6 / GP7 |
| EN de los 3 motores | GP5/4, GP17/16, GP2/3 | **ya no los maneja la Pico** |
| ENC1 A/B | GP11 / GP10 | GP10 / GP11 |
| ENC2 A/B | GP12 / GP13 | GP12 / GP13 |
| ENC3 A/B | GP8 / GP9 | GP14 / GP15 |
| Límites | R1 GP21, R2 GP20, P GP19 | LIMIT1 GP17, LIMIT2 GP18, LIMIT3 GP19, LIMIT4 GP20, LIMIT5 GP21, LIMIT6 GP22 |
| I2C1 con el STM32 | — | SCL GP4, SDA GP5 (pull-ups 5.1k a 3V3) |
| Servo / bomba | — | GP8 / GP9 |

Supuesto: M1 = R1, M2 = R2, M3 = P (coincide con ENC1/ENC2/ENC3).

Las reglas del código se cumplen: los encoders van en GPIO consecutivos y cada pareja de PWM está en una sola slice (0, 1, 3).

**Importante:** si se flashea el firmware actual en la PCB sin cambiarlo, la Pico pone como salidas GPIO los antiguos EN (GP2/GP3 = PWM de M2, GP4/GP5 = I2C, GP16/GP17). Además usa como PWM los pines GP0/1 y GP6/7, que ahora van a otros motores. **No flashearlo tal cual.**

## 3. Pines del STM32F030C8T6

| Función | Pin |
|---|---|
| ADC_IN0..IN5 (corriente) | PA0..PA5 (canales ADC 0–5) |
| M1_EN / M2_EN / M3_EN | PB12 / PB13 / PB14 |
| I2C1 con la Pico | PB6 SCL / PB7 SDA |
| USART1 al FC41D | PA9 TX → MAIN_RXD, PA10 RX ← MAIN_TXD |
| CEN del FC41D | PB8 |
| Cristal | HSE 8 MHz en PF0/PF1 |
| Programación | SWD (PA13/PA14) en J1, NRST con botón SW1, BOOT0 a GND |

Mapeo ADC → motor **(supuesto, por el orden del conector J5)**: M1 = IN0/IN1, M2 = IN2/IN3, M3 = IN4/IN5.

## 4. Puntos para revisar en el hardware

1. **Rango del ADC.** Los pines del STM32 en modo analógico **no toleran 5 V**. La señal de corriente que llega por J5 tiene que estar entre 0 y 3.3 V con la corriente máxima. Hay que revisarlo en la placa de potencia (resistencia de IS).
2. **Servo y bomba en la misma slice PWM.** GP8 y GP9 son la slice 4, así que comparten frecuencia. Un servo necesita 50 Hz, por lo que la bomba también quedaría a 50 Hz. Para una bomba de vacío que solo se prende y se apaga no importa. Si se quiere variar su velocidad sin ruido, convendría separarlas.
3. **Sentido de los encoders.** ENC1 tiene A/B invertidos respecto a la baquelita (GP10/GP11 en vez de GP11/GP10). Al código le da igual qué pin es A y cuál es B, pero si el cableado físico cambió, `encoder_sign` puede cambiar de signo. Hay que comprobarlo eje por eje antes de cerrar el lazo.
4. NET_STATUS del FC41D maneja el LED D1 directamente. El STM32 no lo lee, así que el estado de la red se tiene que pedir con comandos AT.

5. **I2C de la Pico invertido.** En el esquemático, GP4 = I2C1_SCL y GP5 = I2C1_SDA. En el RP2040 el I2C por hardware usa GP4 = SDA y GP5 = SCL (los pares son SDA), así que con este ruteo no se puede usar. **Solución en firmware:** I2C por software en esos pines (funciona igual a 100 kHz). En una próxima revisión de la PCB, cruzar esas dos pistas.

## 4b. Placa de potencia (`Schematic_Power.pdf`)

- Cada motor tiene un **puente H completo hecho con 2 medios puentes** de pines VS/IN/INH/IS/SR/OUT/GND, de la familia Infineon NovalithIC (BTN8982 / BTN7960) **(supuesto: el esquemático no dice el número de parte)**. M1 usa U3 + U2, M2 usa U5 + U4 y M3 usa U7 + U6.
- M*_INA va al IN de un medio puente y M*_INB al del otro. **El mismo M*_EN va a los dos INH.** Es la misma lógica que el módulo de la baquelita (un PWM por sentido), así que el control del motor en la Pico no cambia, solo los pines.
- Esto confirma el mapeo de corriente: **ADC_IN0/IN1 = M1, IN2/IN3 = M2, IN4/IN5 = M3**. Cada ADC lee el pin IS de un medio puente a través de una resistencia a GND (R12, R10, R20, R18, R28, R26) y un capacitor.
- **Consecuencia para el firmware.** El pin IS solo da corriente mientras el lado alto de ese medio puente conduce. Por eso la corriente del motor es la mayor de las dos lecturas (la del sentido activo), multiplicada por el duty. El STM32 no conoce el duty, así que se mide un valor promediado con filtro.
- **En falla (sobretemperatura o corto), el chip lleva IS a una corriente fija de varios mA.** Con la resistencia de sensado eso puede pasar de 3.3 V. Hay que elegir R_IS para que en falla el pin del STM32 no supere unos 3.3 V. El firmware puede detectar ese nivel como "falla del driver".
- Los valores de las resistencias y capacitores dicen "Comment" en el PDF. **Falta saber R_IS y la parte exacta** para convertir el ADC a amperios.
- Bomba de vacío: un MOSFET Q2 controlado por PUMP_PWM, a 12 V. Servo: alimentado a 5 V, señal SERVO_PWM con una resistencia en serie. Encoders: conectores P1–P3 a 5 V.
- Entrada de 12 V con protección contra polaridad inversa (Q1) y TVS. Un buck LM22678 genera 5 V.

## 4c. Conversión de corriente (IFX007T, R_IS = 470 Ω)

Valores típicos de la familia NovalithIC, **sin verificar contra la hoja de datos del IFX007T** (no se pudo descargar desde aquí): razón de sensado dkILIS ≈ 20 000 y corriente de falla en IS ≈ 4–5 mA.

| Corriente del motor | I_IS | V en el ADC | Cuentas (12 bits, 3.3 V) |
|---|---|---|---|
| 1 A | 50 µA | 24 mV | ~29 |
| 2 A (normal máx.) | 100 µA | 47 mV | ~58 |
| 3 A (umbral sostenido) | 150 µA | 71 mV | ~87 |
| 4.5 A (umbral de pico) | 225 µA | 106 mV | ~131 |
| Falla del driver | ~4.5 mA | ~2.1 V | ~2600 |

- **Seguro:** en falla el pin llega a unos 2.1 V, por debajo de 3.3 V. Con los 1 kΩ que recomienda Infineon llegaría a ~4.5 V, así que **470 Ω está bien elegido**.
- **Resolución baja:** con 470 Ω, 1 A son unas 29 cuentas, y además el IS tiene un offset y una tolerancia grandes con corrientes bajas. El firmware va a calibrar el cero al arrancar (con los EN apagados) y a promediar. Los umbrales hay que **ajustarlos midiendo una vez con una pinza o un multímetro**.
- El firmware distingue "sobrecorriente" (≈ 87–131 cuentas sostenidas) de "falla del driver" (> ~1.5 V).

## 5. Plan de software propuesto

**A. Pico (cambio sobre el repo actual)**
- `axis_config.h`: los pines nuevos de la tabla 2.
- `MotorDriver`: quitar los EN. Para habilitar un eje, la Pico se lo pide al STM32 por I2C.
- La Pico manda un latido por I2C. Si el STM32 deja de recibirlo, apaga los EN.
- La Pico lee del STM32 el estado de falla. Si hay falla por sobrecorriente, el eje pasa a `Fault`.
- Nuevo: salida para el servo y la bomba, y un comando serie para manejarlos.
- `prueba_p.cpp`: los mismos pines nuevos.

**B. STM32 (firmware nuevo)**
- ADC con DMA: lee continuamente los 6 canales, con filtro.
- Protección: si la corriente pasa del umbral de pico, corta de inmediato. Si pasa del umbral continuo durante más de N ms, también corta. Al cortar baja el EN de ese motor y queda enclavado hasta que llegue un reset por I2C.
- Watchdog: el IWDG reinicia el STM32 si se cuelga, y como los EN tienen pull-down, los motores se apagan. Además corta todo si se pierde el latido de la Pico.
- Esclavo I2C con registros: enable por eje, estado de falla, corrientes y umbrales.
- FC41D por AT (USART1): conectarse al Wi-Fi y publicar la telemetría (corrientes, fallas, estado).

## 6. Lo que falta definir

1. ¿M1/M2/M3 = R1/R2/P? ¿Qué LIMIT corresponde a cada eje? (Hay 6: ¿uno por cada extremo?)
2. ~~Driver~~ Respondido por el BOM: **IFX007T** (Infineon) y **R_IS = 470 Ω**. Ver sección 4c.
3. ~~Corriente máxima~~ Respondido: el motor se traba a 5.5 A (stall), trabajo normal ≤ 1.5–2 A. Umbrales propuestos: **> 3 A sostenido 500 ms → corte**, **> 4.5 A durante 20 ms → corte inmediato** (cubre un eje trabado o un choque). Configurables por I2C.
4. ~~Datos~~ Respondido: **MQTT**. Por defecto broker público broker.hivemq.com:1883 para pruebas; topics `scara/telemetria` (JSON cada 1 s) y `scara/falla` (al instante). Configurable.
5. Dónde guardar el código: una rama o fork del repo del compañero, o un repo nuevo.
