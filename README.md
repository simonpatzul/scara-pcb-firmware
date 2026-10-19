# SCARA RRP · software del robot con la PCB nueva

Software del robot SCARA RRP para la PCB nueva (tarjeta de control + tarjeta de potencia). Reemplaza la versión que corría en la baquelita. Hay una carpeta por dispositivo:

| Carpeta | Dispositivo | Qué hay |
|---|---|---|
| `alto_nivel/` | PC con ROS 2 | Workspace de ROS 2: bridge serie, teleop, punto a punto, URDF y la secuencia de la Estación 1 (pick & place). Parte de [jer0valencia/SCARA_RRP](https://github.com/jer0valencia/SCARA_RRP) (commit 26dd87b). Ver [`alto_nivel/README.md`](alto_nivel/README.md) |
| `bajo_nivel/pico/` | Raspberry Pi Pico | Control de los 3 ejes, servo y bomba. Parte de [jer0valencia/SCARA_RRP_](https://github.com/jer0valencia/SCARA_RRP_) (commit a45484c), adaptado a la PCB |
| `bajo_nivel/stm32/` | STM32F030C8T6 | Protección de corriente, enables de los drivers y telemetría MQTT por el FC41D |
| `web/` | PC o servidor | Página SCADA: `frontend/` (la página) y `backend/` (servidor con historial). Ver [`web/README.md`](web/README.md) |
| `docs/` | — | Análisis de los esquemáticos (`analisis_pcb.md`), formato de los mensajes MQTT (`mqtt.md`) y documentación general (`documentacion_scara.html`) |

## Cómo encaja todo

```
 PC (ROS 2, bridge.py) --USB serie "r1,r2,p[,servo,bomba]"--> Pico ---PWM INA/INB---> IFX007T x6 ---> motores
                                                 |                         ^ INH (EN)    | IS (corriente)
                                                 |  I2C (latido 20 ms)     |             v
                                                 +----------------------> STM32 <--------+
                                                                            |
                                                                 USART  --> FC41D --Wi-Fi--> broker MQTT --> web (backend + página SCADA)
```

- **La Pico** controla los ejes igual que antes (RST a 100 Hz, home, watchdog de 200 ms del PC). La diferencia es que ya no maneja los EN de los drivers: se los pide al STM32.
- **El STM32** enciende un EN solo si la Pico lo pide, no hay falla y el latido de la Pico llegó hace menos de 100 ms. Mide la corriente de cada motor cada 1 ms y corta el EN si pasa de 3 A durante 500 ms, de 4.5 A durante 20 ms, o si el IFX007T reporta falla (sobretemperatura o corto). La falla queda enclavada hasta que la Pico se reinicia.
- Si el STM32 se cuelga, su watchdog lo reinicia en ~100 ms, y mientras tanto las resistencias de 10k de la placa bajan los EN.

## Qué cambió respecto a la baquelita (Pico)

| | Baquelita | PCB |
|---|---|---|
| PWM R1 / R2 / P | GP6-7 / GP14-15 / GP1-0 | GP0-1 / GP2-3 / GP7-6 |
| Encoder R1 / R2 / P | GP11-10 / GP12-13 / GP8-9 | GP10-11 / GP12-13 / GP14-15 |
| Enables | 6 pines de la Pico | los maneja el STM32 (I2C, GP5 = SDA, GP4 = SCL) |
| Finales de carrera | 1 por eje (GP21, GP20, GP19) | 2 por eje, en orden: R1 = GP17/18, R2 = GP19/20, P = GP21/22 (home / otro extremo) |
| Nuevo | | servo (GP8) y bomba de vacío (GP9) |

Además:
- **Finales de carrera en control normal:** el eje nunca empuja contra un final de carrera presionado. Si durante el home toca el del otro extremo, el eje queda en falla.
- **Servo y bomba:** la línea serie acepta `r1,r2,p,servo,bomba` (servo en grados 0–180, bomba de 0 a 1). Con 3 campos funciona como antes.
- **Fallas del STM32:** si el STM32 corta un motor, ese eje pasa a `F` en la respuesta serie y se imprime `# FALLA <eje>: <causa>`.
- `prueba_p.cpp` se reemplazó por `prueba_eje.cpp`, que prueba cualquiera de los 3 ejes.

## Compilar y grabar

**Pico** (necesita el [Pico SDK](https://github.com/raspberrypi/pico-sdk) y `PICO_SDK_PATH`):

```
cd bajo_nivel/pico && mkdir build && cd build
cmake .. && make -j4
```

Salen dos archivos: `scara_fw.uf2` (firmware) y `scara_prueba.uf2` (prueba de ejes). Se arrastran a la Pico en modo BOOTSEL.

**STM32** (necesita `arm-none-eabi-gcc`; para grabar, un ST-Link en el conector SWD J1):

```
cd bajo_nivel/stm32
cp inc/wifi_config.example.h inc/wifi_config.h   # y poner la red Wi-Fi y el broker
make
make flash          # o grabar build/proteccion.bin con STM32CubeProgrammer en 0x08000000
```

Las pruebas de la lógica de protección y del JSON corren en el PC: `make -C bajo_nivel/stm32/test`.

## Primera vez en la PCB: orden recomendado

1. **Grabar el STM32**, alimentar solo la tarjeta de control y comprobar con un broker (por ejemplo [MQTT Explorer](https://mqtt-explorer.com)) que llegan mensajes a `scara/estado` con `"ciclo":"SIN_PICO"`.
2. **Grabar `scara_prueba.uf2`** en la Pico y abrir el monitor serie (115200). Debe decir `STM32: OK`.
3. **Probar cada eje** con 12 V conectados y el robot sin carga:
   - Con `1`, `2` y `3` se elige el eje; con `+` y `-` se sube o se baja el voltaje de a 1 V; con `0` se para.
   - Con voltaje **positivo**, la articulación debe **aumentar**. Si disminuye, hay que cambiar el signo de `encoder_sign` de ese eje en `bajo_nivel/pico/config/axis_config.h`.
   - Con el voltaje del mismo signo que `home_volts` (negativo), el eje debe ir hacia el final de carrera de **home**.
   - Hay que apretar cada final de carrera con la mano y comprobar que cambie `home=` / `fin=`.
   - Con el eje frenado con la mano, la corriente debe subir y el STM32 debe cortar pasados los 3 A.
4. **Medir la corriente real una vez** con un multímetro o una pinza amperimétrica y compararla con la que reporta el STM32. Si no coinciden, ajustar `K_ILIS` en `bajo_nivel/stm32/inc/config.h`. El datasheet del IFX007T da 19 500 como típico, con 15 000–24 000 de rango (±23 %).
5. **Grabar `scara_fw.uf2`** y hacer el home como antes.

## Cosas del hardware a tener en cuenta

- **I2C de la Pico:** en el esquemático, GP4 quedó como SCL y GP5 como SDA. El I2C por hardware del RP2040 los usa al revés (los pines pares son SDA). Por eso la Pico usa I2C por software (`soft_i2c.cpp`) y funciona igual. Si se hace otra revisión de la PCB, conviene cruzar esas dos pistas.
- **Servo y bomba:** comparten slice PWM y por eso los dos trabajan a 50 Hz. Para la bomba alcanza (prendida, apagada o a media potencia).
- **Resolución de la corriente:** con R_IS = 470 Ω, 1 A da unas 30 cuentas del ADC. Alcanza para proteger, pero no para medir con precisión. En falla, el pin IS llega a 1.93–2.87 V, dentro del rango del STM32.
- **Offset del IS:** cada IFX007T suma entre 30 y 385 µA de offset, y solo con el EN encendido; con el EN apagado el IS da 0. Por eso el STM32 no calibra el cero al arrancar: lo aprende mientras el motor trabaja, de los momentos en que ese medio puente no conduce (ver `protection.c`).
- **Comandos AT del FC41D** (`bajo_nivel/stm32/src/fc41d.c`): siguen el manual AT V1.0 del FC41D (`AT+QSTAAPINFO` sin comillas, `AT+QMTCFG`, `AT+QMTOPEN`, `AT+QMTCONN`, `AT+QMTPUBRAW` para el JSON), pero no se probaron con el módulo real. Si no conecta, hay que revisar las respuestas en el monitor serie del módulo y ajustar las cadenas.
