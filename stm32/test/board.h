#pragma once
// Sustituto de board.h para probar la lógica en el PC (sin hardware)
#include <stdint.h>
#include <stdbool.h>
#define NUM_MOTORS 3
#define NUM_ADC_CH 6
#define ADC_SCANS 48
extern volatile uint16_t adc_buf[ADC_SCANS][NUM_ADC_CH];
extern volatile uint32_t ms_ticks;
extern bool en_pin[NUM_MOTORS];
static inline void motor_en(int m, bool on) { en_pin[m] = on; }
static inline void adc_read_internal(int16_t* t, uint16_t* v) { *t = 312; *v = 3305; }
static inline uint32_t millis(void) { return ms_ticks; }
#define __disable_irq()
#define __enable_irq()
