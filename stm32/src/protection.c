#include "protection.h"
#include "board.h"
#include "config.h"
#include "shared.h"

// mA del motor por cada cuenta del ADC: (3300 mV / 4095) / R_IS * K_ILIS
#define MA_PER_COUNT  (3300.0f / 4095.0f / R_IS_OHM * K_ILIS)
#define MV_PER_COUNT  (3300.0f / 4095.0f)

// Offset del IS de cada medio puente, aprendido con el EN encendido (ver config.h y el paso 5)
static float offset[NUM_ADC_CH];
static bool offset_known[NUM_ADC_CH];
static bool cal_done = false;      // pasó STARTUP_MS desde el arranque

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
    // 1. Máximo, mínimo y promedio de cada canal en la ventana del DMA (~1 ms, ~20 periodos del PWM).
    //    El IS da corriente + offset mientras conduce el lado alto (parte "on" del PWM) y solo el offset
    //    mientras conduce el lado bajo: el máximo menos el offset es la corriente del motor; el promedio
    //    sirve para ver el nivel fijo de falla del driver.
    uint16_t maxv[NUM_ADC_CH] = {0};
    uint16_t minv[NUM_ADC_CH];
    uint32_t sum[NUM_ADC_CH] = {0};
    for (int c = 0; c < NUM_ADC_CH; ++c) minv[c] = 0xFFFF;
    for (int s = 0; s < ADC_SCANS; ++s) {
        for (int c = 0; c < NUM_ADC_CH; ++c) {
            const uint16_t v = adc_buf[s][c];
            if (v > maxv[c]) maxv[c] = v;
            if (v < minv[c]) minv[c] = v;
            sum[c] += v;
        }
    }

    // 2. Arranque: esperar a que el ADC esté estable antes de aceptar pedidos de EN
    if (!cal_done && millis() >= STARTUP_MS) cal_done = true;

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
    //    Offset de cada canal (solo con el EN encendido; con el EN apagado el IS da 0):
    //    - si la ventana muestra conmutación (max - min > SWITCH_COUNTS), el mínimo es el offset;
    //    - si la Pico manda casi 0 V a ese motor, todo el canal es offset.
    //    Se promedia (~100 ms). El cero de cada ventana es el menor entre su mínimo y el offset aprendido:
    //    así el medio puente que no conduce da 0, y el que conduce al 100 % (sin parte "off") se mide
    //    contra el offset que se aprendió antes.
    const bool pico_fresh = millis() - pico_last_block_ms < 300 && pico_last_block_ms != 0;
    int32_t counts_m[NUM_MOTORS];
    for (int m = 0; m < NUM_MOTORS; ++m) {
        const int16_t u_mv = get16(REG_PICO_VOLT + 2 * m);
        const bool idle = pico_fresh && u_mv > -IDLE_MV_PICO && u_mv < IDLE_MV_PICO;
        int32_t counts = 0;
        for (int k = 0; k < 2; ++k) {
            const int c = 2 * m + k;
            if (en_active & (1u << m)) {
                float sample = -1.0f;
                if (maxv[c] - minv[c] > SWITCH_COUNTS) sample = minv[c];
                else if (idle) sample = (float)sum[c] / ADC_SCANS;
                if (sample >= 0.0f) {
                    offset[c] = offset_known[c] ? offset[c] + 0.01f * (sample - offset[c]) : sample;
                    offset_known[c] = true;
                }
            }
            float zero = minv[c];
            if (offset_known[c] && offset[c] < zero) zero = offset[c];
            const int32_t cc = (int32_t)maxv[c] - (int32_t)(zero + 0.5f);
            if (cc > counts) counts = cc;   // solo una mitad conduce: la del sentido activo
        }
        counts_m[m] = counts;
    }

    for (int m = 0; m < NUM_MOTORS; ++m) {
        const int a = 2 * m, b = 2 * m + 1;
        const int32_t counts = counts_m[m];
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
