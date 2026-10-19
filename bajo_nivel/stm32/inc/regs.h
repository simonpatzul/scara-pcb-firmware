#pragma once
// Mapa de registros I2C del STM32 de protección (esclavo, dirección STM32_I2C_ADDR).
// La Pico es el maestro. Este archivo es idéntico en pico/config/stm32_regs.h: si cambias uno, cambia el otro.
//
// Protocolo: el primer byte de una escritura es el número de registro; los siguientes se escriben
// a partir de ahí (autoincremento). Para leer: escribir solo el número de registro y luego leer N bytes.
// Los valores de 16/32 bits van en little endian.

#define STM32_I2C_ADDR      0x30
#define STM32_WHO_AM_I_VAL  0xA5

// ---- Solo lectura (los escribe el STM32) ----
#define REG_WHO_AM_I    0x00  // siempre STM32_WHO_AM_I_VAL
#define REG_VERSION     0x01  // versión del protocolo
#define REG_STATUS      0x02  // bits 0-2: EN encendido de M1..M3; bit 3: calibración lista;
                              // bit 4: Wi-Fi conectado; bit 5: MQTT conectado; bit 6: se perdió el latido (pegajoso)
#define REG_FAULT       0x03  // bits 0-2: falla enclavada en M1..M3
#define REG_CAUSE       0x04  // 3 bytes, causa de la falla de cada motor (FAULT_*)
#define REG_CURRENT     0x08  // 3 x int16, corriente de cada motor en mA
#define REG_STM32_TEMP  0x0E  // int16, temperatura del STM32 en décimas de °C

// ---- Escritura desde la Pico ----
#define REG_CONTROL     0x10  // bits 0-2: la Pico pide encender el EN de M1..M3. Escribirlo cuenta como latido
#define REG_CLEAR       0x11  // bits 0-2: borrar la falla de M1..M3; bit 7: borrar el bit de latido perdido

// Telemetría de la Pico (bloque de 22 bytes desde REG_PICO_CYCLE). El STM32 solo la reenvía por MQTT.
#define REG_PICO_CYCLE  0x20  // uint8, CYCLE_*
#define REG_PICO_STATE  0x21  // 3 x char: 'D', 'H', 'R', 'F'
#define REG_PICO_POS    0x24  // 3 x int16: R1/R2 en mrad, P en centésimas de mm
#define REG_PICO_VOLT   0x2A  // 3 x int16: voltaje mandado a cada motor en mV
#define REG_PICO_TEMP   0x30  // int16: temperatura del RP2040 en décimas de °C
#define REG_PICO_LOOP   0x32  // uint32: contador del lazo de control (núcleo 1)
#define REG_PICO_LIMITS 0x36  // uint8: bits 0-5 = LIMIT1..LIMIT6
#define PICO_BLOCK_LEN  23    // 0x20..0x36

// ---- Configuración (lectura y escritura) ----
#define REG_CFG_I_SUST  0x40  // uint16 mA,  corriente sostenida máxima
#define REG_CFG_T_SUST  0x42  // uint16 ms,  tiempo por encima antes de cortar
#define REG_CFG_I_PEAK  0x44  // uint16 mA,  corriente de pico máxima
#define REG_CFG_T_PEAK  0x46  // uint16 ms

#define REG_COUNT       0x48

// Causas de falla
#define FAULT_NONE      0
#define FAULT_SUSTAINED 1   // > I_SUST durante T_SUST
#define FAULT_PEAK      2   // > I_PEAK durante T_PEAK
#define FAULT_DRIVER    3   // el IFX007T puso IS en nivel de falla (sobretemperatura, corto)
#define FAULT_LINK      4   // la Pico dejó de mandar el latido

// Ciclo del programa de la Pico
#define CYCLE_BOOT      0
#define CYCLE_WAIT      1
#define CYCLE_HOME_R1   2
#define CYCLE_HOME_R2   3
#define CYCLE_HOME_P    4
#define CYCLE_READY     5
#define CYCLE_HOLD      6
#define CYCLE_FAULT     7
