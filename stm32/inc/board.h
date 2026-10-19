#pragma once
// Hardware de la tarjeta de control (STM32F030C8T6). Pines según el esquemático MCU.SchDoc:
//   PA0..PA5  ADC_IN0..ADC_IN5   corriente (IS) de cada medio puente: M1 = IN0/IN1, M2 = IN2/IN3, M3 = IN4/IN5
//   PB12..14  M1_EN, M2_EN, M3_EN (INH de los dos medios puentes de cada motor; pull-down 10k en la placa)
//   PB6/PB7   I2C1 SCL/SDA, esclavo de la Pico
//   PA9/PA10  USART1 TX/RX hacia el FC41D
//   PB8       CEN del FC41D (alto = módulo encendido)
//   PF0/PF1   cristal de 8 MHz
#include <stdint.h>
#include <stdbool.h>
#include "stm32f0xx.h"

#define NUM_MOTORS   3
#define NUM_ADC_CH   6
#define ADC_SCANS    48      // el DMA guarda las últimas 48 lecturas de los 6 canales (~1 ms)

extern volatile uint16_t adc_buf[ADC_SCANS][NUM_ADC_CH];
extern volatile uint32_t ms_ticks;

void board_init(void);          // reloj a 48 MHz, GPIO, ADC + DMA, USART1, SysTick a 1 kHz
void motor_en(int m, bool on);  // m = 0..2
void fc41d_power(bool on);
void watchdog_start(void);      // IWDG de ~100 ms. Una vez arrancado no se puede detener
void watchdog_kick(void);

// Mide temperatura interna y VDDA. Detiene el escaneo continuo ~50 us; llamar desde SysTick
void adc_read_internal(int16_t* temp_dC, uint16_t* vdda_mV);

static inline uint32_t millis(void) { return ms_ticks; }
