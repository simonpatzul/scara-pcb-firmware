#pragma once
// Protección de sobrecorriente y hombre muerto. Corre cada 1 ms en SysTick y es lo único que toca los EN.
#include <stdint.h>

void protection_init(void);
void protection_tick(void);

// Llamadas desde el esclavo I2C cuando la Pico escribe
void protection_on_control(uint8_t en_request);   // también es el latido
void protection_on_clear(uint8_t mask);
void protection_on_config(void);                  // cambió algún REG_CFG_*
