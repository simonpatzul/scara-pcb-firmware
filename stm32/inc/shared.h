#pragma once
// Estado compartido entre la protección (SysTick), el esclavo I2C (interrupción) y la telemetría (bucle principal).
// La "fuente de verdad" es el mapa de registros: lo que la Pico lee por I2C es lo mismo que sale por MQTT.
#include <stdint.h>
#include <stdbool.h>
#include "regs.h"

extern volatile uint8_t regs[REG_COUNT];

extern volatile uint32_t pico_last_block_ms;   // cuándo llegó el último bloque de telemetría de la Pico
extern volatile uint8_t  event_mask;           // bit m = el motor m cambió de falla (lo consume la telemetría)
extern volatile bool     wifi_ok, mqtt_ok;
extern volatile uint16_t stm32_vdda_mV;

static inline void put16(int reg, int16_t v) {
    regs[reg] = (uint8_t)v;
    regs[reg + 1] = (uint8_t)((uint16_t)v >> 8);
}
static inline int16_t get16(int reg) {
    return (int16_t)(regs[reg] | (regs[reg + 1] << 8));
}
static inline uint32_t get32(int reg) {
    return regs[reg] | (regs[reg + 1] << 8) | ((uint32_t)regs[reg + 2] << 16) | ((uint32_t)regs[reg + 3] << 24);
}
