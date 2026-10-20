#include "i2c_slave.h"
#include "board.h"
#include "shared.h"
#include "protection.h"

volatile uint8_t regs[REG_COUNT];
volatile uint32_t pico_last_block_ms = 0;

static uint8_t ptr = 0;          // registro actual (autoincremento)
static bool have_ptr = false;    // el primer byte de una escritura es el número de registro
static uint8_t wr_start = 0, wr_len = 0;
static uint8_t wr_buf[REG_COUNT];

#define I2C_STUCK_MS 25
static volatile uint32_t busy_since = 0;   // 0 = el bus no está ocupado

static void enable_peripheral(void) {
    I2C1->CR1 = I2C_CR1_ADDRIE | I2C_CR1_RXIE | I2C_CR1_TXIE | I2C_CR1_STOPIE | I2C_CR1_NACKIE |
                I2C_CR1_ERRIE | I2C_CR1_PE;
}

void i2c_slave_init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    // El reloj del I2C1 es HSI (8 MHz). TIMINGR para 100 kHz; en esclavo solo cuentan los tiempos de setup/hold
    I2C1->TIMINGR = 0x2000090E;
    I2C1->OAR1 = I2C_OAR1_OA1EN | (STM32_I2C_ADDR << 1);
    enable_peripheral();
    NVIC_SetPriority(I2C1_IRQn, 0);
    NVIC_EnableIRQ(I2C1_IRQn);

    regs[REG_WHO_AM_I] = STM32_WHO_AM_I_VAL;
    regs[REG_VERSION] = 1;
}

// Aplica una escritura completa (al recibir STOP). Solo se aceptan los registros que la Pico puede escribir
static void commit_write(void) {
    bool cfg = false, block = false;
    for (int i = 0; i < wr_len; ++i) {
        const int r = wr_start + i;
        if (r >= REG_COUNT) break;
        const uint8_t v = wr_buf[i];
        if (r == REG_CONTROL) protection_on_control(v);
        else if (r == REG_CLEAR) protection_on_clear(v);
        else if (r >= REG_PICO_CYCLE && r < REG_PICO_CYCLE + PICO_BLOCK_LEN) { regs[r] = v; block = true; }
        else if (r >= REG_CFG_I_SUST && r < REG_COUNT) { regs[r] = v; cfg = true; }
    }
    if (block) pico_last_block_ms = millis();
    if (cfg) protection_on_config();
}

void i2c_slave_poll(void) {
    if (!(I2C1->ISR & I2C_ISR_BUSY)) { busy_since = 0; return; }
    const uint32_t now = millis();
    if (busy_since == 0) { busy_since = now ? now : 1; return; }
    if (now - busy_since < I2C_STUCK_MS) return;
    // Bus trabado (la Pico se reinició a mitad de un mensaje, ruido...): apagar y volver a prender el I2C
    // suelta SCL y SDA. RM0360: PE tiene que quedar en 0 al menos 3 ciclos de APB.
    __disable_irq();
    I2C1->CR1 &= ~I2C_CR1_PE;
    for (volatile int i = 0; i < 10; ++i) {}
    wr_len = 0;
    have_ptr = false;
    enable_peripheral();
    __enable_irq();
    busy_since = 0;
}

void I2C1_IRQHandler(void) {
    const uint32_t isr = I2C1->ISR;

    if (isr & I2C_ISR_ADDR) {
        if (isr & I2C_ISR_DIR) {
            I2C1->ISR |= I2C_ISR_TXE;      // lectura del maestro: vaciar TXDR para mandar datos frescos
        } else {
            have_ptr = false;              // escritura del maestro: empieza por el número de registro
            wr_len = 0;
        }
        I2C1->ICR = I2C_ICR_ADDRCF;
    }

    if (isr & I2C_ISR_RXNE) {
        const uint8_t b = (uint8_t)I2C1->RXDR;
        if (!have_ptr) {
            ptr = b;
            have_ptr = true;
            wr_start = b;
        } else if (wr_len < sizeof(wr_buf)) {
            wr_buf[wr_len++] = b;
        }
    }

    if (isr & I2C_ISR_TXIS) {
        I2C1->TXDR = ptr < REG_COUNT ? regs[ptr] : 0xFF;
        ptr++;
    }

    if (isr & I2C_ISR_NACKF) I2C1->ICR = I2C_ICR_NACKCF;   // el maestro no quiere más bytes

    if (isr & I2C_ISR_STOPF) {
        I2C1->ICR = I2C_ICR_STOPCF;
        if (wr_len > 0) commit_write();
        wr_len = 0;
    }

    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
        I2C1->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
        wr_len = 0;
        have_ptr = false;
    }
}
