#include "board.h"

volatile uint16_t adc_buf[ADC_SCANS][NUM_ADC_CH];
volatile uint32_t ms_ticks = 0;

// Datos de calibración de fábrica (RM0360, sección 13.8)
#define TS_CAL1      (*(const uint16_t*)0x1FFFF7B8)   // lectura del sensor a 30 °C con VDDA = 3.3 V
#define VREFINT_CAL  (*(const uint16_t*)0x1FFFF7BA)   // lectura de VREFINT con VDDA = 3.3 V

static void clock_init(void) {
    // HSE de 8 MHz x 6 = 48 MHz. Si el cristal no arranca, HSI/2 x 12 = 48 MHz.
    RCC->CR |= RCC_CR_HSEON;
    uint32_t timeout = 100000;
    while (!(RCC->CR & RCC_CR_HSERDY) && --timeout) {}
    const bool hse_ok = timeout != 0;

    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;   // 1 wait state para 48 MHz

    RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL);
    if (hse_ok) RCC->CFGR |= RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL6;
    else        RCC->CFGR |= RCC_CFGR_PLLSRC_HSI_DIV2 | RCC_CFGR_PLLMUL12;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SystemCoreClock = 48000000;
}

static void gpio_init(void) {
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_DMAEN;

    // Primero los EN en bajo y como salida: los motores quedan apagados desde el arranque
    GPIOB->BRR = (1u << 12) | (1u << 13) | (1u << 14) | (1u << 8);
    for (int pin = 12; pin <= 14; ++pin)
        GPIOB->MODER = (GPIOB->MODER & ~(3u << (2 * pin))) | (1u << (2 * pin));
    GPIOB->MODER = (GPIOB->MODER & ~(3u << 16)) | (1u << 16);   // PB8 = CEN, salida

    // PA0..PA5 analógicos
    GPIOA->MODER |= 0xFFFu;

    // PA9/PA10 = USART1 (AF1)
    GPIOA->MODER = (GPIOA->MODER & ~((3u << 18) | (3u << 20))) | (2u << 18) | (2u << 20);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFu << 4) | (0xFu << 8))) | (1u << 4) | (1u << 8);

    // PB6/PB7 = I2C1 (AF1), open drain. Los pull-ups (5.1k) están en la placa
    GPIOB->MODER = (GPIOB->MODER & ~((3u << 12) | (3u << 14))) | (2u << 12) | (2u << 14);
    GPIOB->OTYPER |= (1u << 6) | (1u << 7);
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~((0xFu << 24) | (0xFu << 28))) | (1u << 24) | (1u << 28);
}

static void adc_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADCEN;
    ADC1->CFGR2 = ADC_CFGR2_CKMODE_1;            // reloj del ADC = PCLK/4 = 12 MHz (máximo 14)
    ADC1->CR |= ADC_CR_ADCAL;                    // autocalibración (con el ADC apagado)
    while (ADC1->CR & ADC_CR_ADCAL) {}
    ADC->CCR |= ADC_CCR_TSEN | ADC_CCR_VREFEN;   // sensor de temperatura y referencia interna

    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    while (!(ADC1->ISR & ADC_ISR_ADRDY)) {}

    // Escaneo continuo de PA0..PA5 con muestreo corto (13.5 ciclos, ~2 us por canal, ~13 us por vuelta).
    // Así las lecturas caen en distintos puntos del PWM de 20 kHz y se puede tomar el máximo.
    ADC1->CFGR1 = ADC_CFGR1_CONT | ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG;
    ADC1->CHSELR = 0x3F;
    ADC1->SMPR = 2;

    DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR;
    DMA1_Channel1->CMAR = (uint32_t)adc_buf;
    DMA1_Channel1->CNDTR = ADC_SCANS * NUM_ADC_CH;
    DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_CIRC | DMA_CCR_EN;
    ADC1->CR |= ADC_CR_ADSTART;
}

void adc_read_internal(int16_t* temp_dC, uint16_t* vdda_mV) {
    // Detener el escaneo de corriente
    ADC1->CR |= ADC_CR_ADSTP;
    while (ADC1->CR & ADC_CR_ADSTP) {}
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;

    // Conversión simple de canal 16 (temperatura) y 17 (VREFINT). El sensor necesita > 4 us de muestreo
    ADC1->CFGR1 &= ~(ADC_CFGR1_CONT | ADC_CFGR1_DMAEN);
    ADC1->CHSELR = (1u << 16) | (1u << 17);
    ADC1->SMPR = 7;                                // 239.5 ciclos = 20 us
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
    ADC1->CR |= ADC_CR_ADSTART;
    while (!(ADC1->ISR & ADC_ISR_EOC)) {}
    const uint32_t ts = ADC1->DR;
    while (!(ADC1->ISR & ADC_ISR_EOC)) {}
    const uint32_t vref = ADC1->DR;

    // Volver al escaneo continuo
    ADC1->CHSELR = 0x3F;
    ADC1->SMPR = 2;
    ADC1->CFGR1 |= ADC_CFGR1_CONT | ADC_CFGR1_DMAEN;
    DMA1_Channel1->CNDTR = ADC_SCANS * NUM_ADC_CH;
    DMA1_Channel1->CCR |= DMA_CCR_EN;
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
    ADC1->CR |= ADC_CR_ADSTART;

    if (vref == 0) return;
    const int32_t vdda = 3300 * (int32_t)VREFINT_CAL / (int32_t)vref;
    const int32_t v_sense = (int32_t)ts * vdda / 4095;          // mV
    const int32_t v_30 = (int32_t)TS_CAL1 * 3300 / 4095;        // mV a 30 °C
    *vdda_mV = (uint16_t)vdda;
    *temp_dC = (int16_t)(300 + (v_30 - v_sense) * 100 / 43);    // pendiente típica 4.3 mV/°C
}

static void usart_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    USART1->BRR = (48000000 + 115200 / 2) / 115200;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    NVIC_SetPriority(USART1_IRQn, 2);
    NVIC_EnableIRQ(USART1_IRQn);
}

void board_init(void) {
    clock_init();
    gpio_init();
    adc_init();
    usart_init();
    SysTick_Config(48000);           // 1 ms
    NVIC_SetPriority(SysTick_IRQn, 1);
}

void motor_en(int m, bool on) {
    const uint32_t bit = 1u << (12 + m);
    if (on) GPIOB->BSRR = bit;
    else    GPIOB->BRR = bit;
}

void fc41d_power(bool on) {
    if (on) GPIOB->BSRR = 1u << 8;
    else    GPIOB->BRR = 1u << 8;
}

void watchdog_start(void) {
    IWDG->KR = 0xCCCC;          // arranca el IWDG (y el LSI de ~40 kHz)
    IWDG->KR = 0x5555;          // permite escribir PR y RLR
    IWDG->PR = 3;               // /32 -> 1.25 kHz
    IWDG->RLR = 125;            // 100 ms
    while (IWDG->SR) {}
    IWDG->KR = 0xAAAA;
}

void watchdog_kick(void) {
    IWDG->KR = 0xAAAA;
}
