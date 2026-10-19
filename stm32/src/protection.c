#include "protection.h"
#include "board.h"
#include "config.h"
#include "shared.h"

// mA del motor por cada cuenta del ADC: (3300 mV / 4095) / R_IS * K_ILIS
#define MA_PER_COUNT  (3300.0f / 4095.0f / R_IS_OHM * K_ILIS)
#define MV_PER_COUNT  (3300.0f / 4095.0f)

static uint32_t cal_sum[NUM_ADC_CH];
static uint32_t cal_ticks = 0;
static uint16_t offset[NUM_ADC_CH];
static bool cal_done = false;

static uint16_t cnt_sust[NUM_MOTORS], cnt_peak[NUM_MOTORS], cnt_drv[NUM_MOTORS];
static uint16_t i_sust, t_sust, i_peak, t_peak;

// Lo escribe la interrupción del I2C y lo lee el tick
static volatile uint8_t en_request = 0;
static volatile uint8_t clear_request = 0;
static volatile uint32_t last_hb_ms = 0;
static volatile bool hb_seen = false;
static bool hb_lost = false;

static uint8_t fault = 0;
static uint8_t en_active = 0;

void protection_init(void) {
    put16(REG_CFG_I_SUST, I_SUST_MA);
    put16(REG_CFG_T_SUST, T_SUST_MS);
    put16(REG_CFG_I_PEAK, I_PEAK_MA);
    put16(REG_CFG_T_PEAK, T_PEAK_MS);
    protection_on_config();
    for (int m = 0; m < NUM_MOTORS; ++m) motor_en(m, false);
}

void protection_on_control(uint8_t req) {
    en_request = req & 0x07;
    last_hb_ms = millis();
    hb_seen = true;
}

void protection_on_clear(uint8_t mask) {
    clear_request |= mask;
}

void protection_on_config(void) {
    // Se ignoran valores absurdos: un cero dejaría al robot sin protección
    const uint16_t is = (uint16_t)get16(REG_CFG_I_SUST), ts = (uint16_t)get16(REG_CFG_T_SUST);
    const uint16_t ip = (uint16_t)get16(REG_CFG_I_PEAK), tp = (uint16_t)get16(REG_CFG_T_PEAK);
    if (is >= 200 && is <= 6000) i_sust = is;
    if (ts >= 10 && ts <= 5000)  t_sust = ts;
    if (ip >= 200 && ip <= 8000) i_peak = ip;
    if (tp >= 1 && tp <= 1000)   t_peak = tp;
    put16(REG_CFG_I_SUST, i_sust);
    put16(REG_CFG_T_SUST, t_sust);
    put16(REG_CFG_I_PEAK, i_peak);
    put16(REG_CFG_T_PEAK, t_peak);
}

static void trip(int m, uint8_t cause) {
    motor_en(m, false);            // primero cortar, después anotar
    en_active &= ~(1u << m);
    if (fault & (1u << m)) return;
    fault |= 1u << m;
    regs[REG_CAUSE + m] = cause;
    event_mask |= 1u << m;
}

void protection_tick(void) {
    // 1. Máximo y promedio de cada canal en la ventana del DMA (~0.9 ms).
    //    El IS solo da corriente mientras conduce el lado alto, o sea durante la parte "on" del PWM:
    //    el máximo es la corriente del motor; el promedio sirve para ver el nivel fijo de falla del driver.
    uint16_t maxv[NUM_ADC_CH] = {0};
    uint32_t sum[NUM_ADC_CH] = {0};
    for (int s = 0; s < ADC_SCANS; ++s) {
        for (int c = 0; c < NUM_ADC_CH; ++c) {
            const uint16_t v = adc_buf[s][c];
            if (v > maxv[c]) maxv[c] = v;
            sum[c] += v;
        }
    }

    // 2. Calibración del cero al arrancar, con todos los EN apagados
    if (!cal_done) {
        for (int c = 0; c < NUM_ADC_CH; ++c) cal_sum[c] += sum[c] / ADC_SCANS;
        if (++cal_ticks >= CAL_MS) {
            for (int c = 0; c < NUM_ADC_CH; ++c) offset[c] = (uint16_t)(cal_sum[c] / cal_ticks);
            cal_done = true;
        }
    }

    // 3. Borrados de falla pedidos por la Pico
    __disable_irq();
    const uint8_t clr = clear_request;
    clear_request = 0;
    __enable_irq();
    if (clr & 0x80) hb_lost = false;
    for (int m = 0; m < NUM_MOTORS; ++m) {
        if ((clr & (1u << m)) && (fault & (1u << m))) {
            fault &= ~(1u << m);
            regs[REG_CAUSE + m] = FAULT_NONE;
            cnt_sust[m] = cnt_peak[m] = cnt_drv[m] = 0;
            event_mask |= 1u << m;
        }
    }

    // 4. Latido de la Pico
    if (hb_seen && millis() - last_hb_ms > HEARTBEAT_TIMEOUT_MS) {
        hb_seen = false;
        hb_lost = true;
        en_request = 0;
        for (int m = 0; m < NUM_MOTORS; ++m)
            if (en_active & (1u << m)) trip(m, FAULT_LINK);
    }

    // 5. Corriente y fallas de cada motor (M1 = canales 0/1, M2 = 2/3, M3 = 4/5)
    for (int m = 0; m < NUM_MOTORS; ++m) {
        const int a = 2 * m, b = 2 * m + 1;
        const int32_t ca = cal_done ? (int32_t)maxv[a] - offset[a] : 0;
        const int32_t cb = cal_done ? (int32_t)maxv[b] - offset[b] : 0;
        int32_t counts = ca > cb ? ca : cb;   // solo una mitad conduce: la del sentido activo
        if (counts < 0) counts = 0;
        const int32_t ma = (int32_t)(counts * MA_PER_COUNT);
        put16(REG_CURRENT + 2 * m, (int16_t)(ma > 32767 ? 32767 : ma));

        const float avg_a = (float)sum[a] / ADC_SCANS * MV_PER_COUNT;
        const float avg_b = (float)sum[b] / ADC_SCANS * MV_PER_COUNT;

        if (!cal_done) continue;
        cnt_drv[m]  = (avg_a > DRIVER_FAULT_MV || avg_b > DRIVER_FAULT_MV) ? cnt_drv[m] + 1 : 0;
        cnt_peak[m] = (ma > i_peak) ? cnt_peak[m] + 1 : 0;
        cnt_sust[m] = (ma > i_sust) ? cnt_sust[m] + 1 : 0;

        if (cnt_drv[m] >= T_DRIVER_FAULT_MS) trip(m, FAULT_DRIVER);
        else if (cnt_peak[m] >= t_peak)      trip(m, FAULT_PEAK);
        else if (cnt_sust[m] >= t_sust)      trip(m, FAULT_SUSTAINED);
    }

    // 6. Enables: solo con calibración lista, pedido de la Pico y sin falla
    for (int m = 0; m < NUM_MOTORS; ++m) {
        const bool on = cal_done && (en_request & (1u << m)) && !(fault & (1u << m));
        motor_en(m, on);
        if (on) en_active |= 1u << m;
        else    en_active &= ~(1u << m);
    }

    // 7. Registros de estado
    regs[REG_STATUS] = en_active | (cal_done ? 0x08 : 0) | (wifi_ok ? 0x10 : 0) |
                       (mqtt_ok ? 0x20 : 0) | (hb_lost ? 0x40 : 0);
    regs[REG_FAULT] = fault;

    // 8. Temperatura y VDDA una vez por segundo
    if (millis() % 1000 == 0) {
        int16_t t = get16(REG_STM32_TEMP);
        uint16_t vdda = 0;
        adc_read_internal(&t, &vdda);
        put16(REG_STM32_TEMP, t);
        stm32_vdda_mV = vdda ? vdda : stm32_vdda_mV;
    }
}
