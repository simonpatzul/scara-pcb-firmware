# SCARA RRP · firmware para la PCB

Firmware del robot SCARA RRP para la PCB nueva (tarjeta de control + tarjeta de potencia). Reemplaza la versión que corría en la baquelita.

| Carpeta | Qué hay |
|---|---|
| `pico/` | Firmware de la Raspberry Pi Pico: control de los 3 ejes. Parte de [jer0valencia/SCARA_RRP_](https://github.com/jer0valencia/SCARA_RRP_) (commit a45484c), adaptado a la PCB |
| `stm32/` | Firmware nuevo del STM32F030C8T6: protección de corriente, enables de los drivers y telemetría MQTT por el FC41D |
| `scada/` | Página SCADA (`index.html`): se abre en el navegador y lee el MQTT del STM32 |
| `docs/` | Análisis de los esquemáticos (`analisis_pcb.md`), formato de los mensajes MQTT (`mqtt.md`) y documentación general (`documentacion_scara.html`) |

El lado del PC (ROS 2) sigue en [jer0valencia/SCARA_RRP](https://github.com/jer0valencia/SCARA_RRP) y funciona sin cambios: el protocolo serie es el mismo.

## Cómo encaja todo

```
 PC (ROS 2, bridge.py) --USB serie "r1,r2,p"--> Pico ---PWM INA/INB---> IFX007T x6 ---> motores
                                                 |                         ^ INH (EN)    | IS (corriente)
                                                 |  I2C (latido 20 ms)     |             v
                                                 +----------------------> STM32 <--------+
                                                                            |
                                                                 USART  --> FC41D --Wi-Fi--> broker MQTT --> SCADA
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
cd pico && mkdir build && cd build
cmake .. && make -j4
```

Salen dos archivos: `scara_fw.uf2` (firmware) y `scara_prueba.uf2` (prueba de ejes). Se arrastran a la Pico en modo BOOTSEL.

**STM32** (necesita `arm-none-eabi-gcc`; para grabar, un ST-Link en el conector SWD J1):

```
cd stm32
cp inc/wifi_config.example.h inc/wifi_config.h   # y poner la red Wi-Fi y el broker
make
make flash          # o grabar build/proteccion.bin con STM32CubeProgrammer en 0x08000000
```

Las pruebas de la lógica de protección y del JSON corren en el PC: `make -C stm32/test`.

## Primera vez en la PCB: orden recomendado

1. **Grabar el STM32**, alimentar solo la tarjeta de control y comprobar con un broker (por ejemplo [MQTT Explorer](https://mqtt-explorer.com)) que llegan mensajes a `scara/estado` con `"ciclo":"SIN_PICO"`.
2. **Grabar `scara_prueba.uf2`** en la Pico y abrir el monitor serie (115200). Debe decir `STM32: OK`.
3. **Probar cada eje** con 12 V conectados y el robot sin carga:
   - Con `1`, `2` y `3` se elige el eje; con `+` y `-` se sube o se baja el voltaje de a 1 V; con `0` se para.
   - Con voltaje **positivo**, la articulación debe **aumentar**. Si disminuye, hay que cambiar el signo de `encoder_sign` de ese eje en `pico/config/axis_config.h`.
   - Con el voltaje del mismo signo que `home_volts` (negativo), el eje debe ir hacia el final de carrera de **home**.
   - Hay que apretar cada final de carrera con la mano y comprobar que cambie `home=` / `fin=`.
   - Con el eje frenado con la mano, la corriente debe subir y el STM32 debe cortar pasados los 3 A.
4. **Medir la corriente real una vez** con un multímetro o una pinza amperimétrica y compararla con la que reporta el STM32. Si no coinciden, ajustar `K_ILIS` en `stm32/inc/config.h`. El valor 20000 es el típico de la familia y no está verificado contra la hoja de datos del IFX007T.
5. **Grabar `scara_fw.uf2`** y hacer el home como antes.

## Cosas del hardware a tener en cuenta

- **I2C de la Pico:** en el esquemático, GP4 quedó como SCL y GP5 como SDA. El I2C por hardware del RP2040 los usa al revés (los pines pares son SDA). Por eso la Pico usa I2C por software (`soft_i2c.cpp`) y funciona igual. Si se hace otra revisión de la PCB, conviene cruzar esas dos pistas.
- **Servo y bomba:** comparten slice PWM y por eso los dos trabajan a 50 Hz. Para la bomba alcanza (prendida, apagada o a media potencia).
- **Resolución de la corriente:** con R_IS = 470 Ω, 1 A da unas 29 cuentas del ADC. Alcanza para proteger, pero no para medir con precisión. En falla, el pin IS llega a unos 2.1 V, dentro del rango del STM32.
- **Comandos AT del FC41D** (`stm32/src/fc41d.c`): siguen el manual de Quectel (`AT+QSTAAPINFO`, `AT+QMTOPEN`, `AT+QMTCONN`, `AT+QMTPUB`), pero no se probaron con el módulo real. Si no conecta, hay que revisar las respuestas en el monitor serie del módulo y ajustar las cadenas.

## Subirlo a GitHub

Crea un repo vacío en GitHub (sin README ni .gitignore) y, desde esta carpeta:

```
git remote add origin https://github.com/simonpatzul/<nombre-del-repo>.git
git push -u origin main
```
