#include "stm32_link.h"
#include <cstdio>
#include <cmath>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "soft_i2c.h"

static SoftI2C bus(5, 4);   // SDA = GP5, SCL = GP4 (ver soft_i2c.h)

constexpr uint32_t PERIOD_MS = 20;
constexpr uint32_t BLOCK_PERIOD_MS = 100;

float rp2040_temperature() {
    adc_select_input(4);
    const float v = adc_read() * 3.3f / 4096.0f;
    return 27.0f - (v - 0.706f) / 0.001721f;   // fórmula del datasheet del RP2040
}

const char* Stm32Link::cause_name(uint8_t c) {
    switch (c) {
        case FAULT_SUSTAINED: return "sobrecorriente sostenida";
        case FAULT_PEAK:      return "pico de corriente";
        case FAULT_DRIVER:    return "falla del driver (temperatura o corto)";
        case FAULT_LINK:      return "se perdio el enlace con la Pico";
        default:              return "ninguna";
    }
}

static void put16(uint8_t* p, int16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((uint16_t)v >> 8); }

static int16_t clamp16(float v) {
    if (std::isnan(v)) return 0;
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)lrintf(v);
}

void Stm32Link::init() {
    adc_init();
    adc_set_temp_sensor_enabled(true);
    bus.init();

    uint8_t who = 0;
    present_ = bus.read_reg(STM32_I2C_ADDR, REG_WHO_AM_I, &who, 1) && who == STM32_WHO_AM_I_VAL;
    if (present_) {
        const uint8_t clr[2] = { REG_CLEAR, 0x87 };   // al arrancar la Pico, borrar fallas viejas
        bus.write(STM32_I2C_ADDR, clr, 2);
    }
}

bool Stm32Link::due() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_ms_ < PERIOD_MS) return false;
    last_ms_ = now;
    return true;
}

void Stm32Link::exchange(uint8_t en_mask) {
    // 1. Pedido de EN (y latido)
    const uint8_t ctrl[2] = { REG_CONTROL, en_mask };
    ok_ = bus.write(STM32_I2C_ADDR, ctrl, 2);

    // 2. Estado: STATUS, FAULT, CAUSE x3, (reservado), CURRENT x3
    uint8_t st[REG_STM32_TEMP - REG_STATUS];
    if (bus.read_reg(STM32_I2C_ADDR, REG_STATUS, st, sizeof(st))) {
        present_ = true;
        fault_ = st[REG_FAULT - REG_STATUS];
        for (int m = 0; m < 3; ++m) {
            cause_[m] = st[REG_CAUSE - REG_STATUS + m];
            const int k = REG_CURRENT - REG_STATUS + 2 * m;
            current_[m] = (int16_t)(st[k] | (st[k + 1] << 8));
        }
    } else {
        ok_ = false;
    }
}

void Stm32Link::service_raw(uint8_t en_mask) {
    if (due()) exchange(en_mask);
}

void Stm32Link::service(Axis* axes, int n) {
    if (!due()) return;
    const uint32_t now = last_ms_;

    uint8_t mask = 0;
    for (int i = 0; i < n && i < 3; ++i)
        if (axes[i].wants_enable()) mask |= (uint8_t)(1u << i);
    exchange(mask);
    for (int i = 0; i < n && i < 3; ++i)
        if ((fault_ & (1u << i)) && axes[i].state() != AxisState::Fault) axes[i].trip();

    // 3. Telemetría para MQTT
    if (now - last_block_ms_ < BLOCK_PERIOD_MS) return;
    last_block_ms_ = now;
    uint8_t blk[1 + PICO_BLOCK_LEN] = {};
    uint8_t* p = blk + 1;   // p[i] corresponde al registro REG_PICO_CYCLE + i
    blk[0] = REG_PICO_CYCLE;
    p[0] = cycle_;
    uint8_t limits = 0;
    for (int i = 0; i < n && i < 3; ++i) {
        const AxisState s = axes[i].state();
        p[REG_PICO_STATE - REG_PICO_CYCLE + i] = s == AxisState::Ready ? 'R' : s == AxisState::Homing ? 'H'
                                               : s == AxisState::Fault ? 'F' : 'D';
        // R1/R2 en mrad, P en centésimas de mm
        const float scale = i < 2 ? 1000.0f : 100.0f;
        put16(&p[REG_PICO_POS - REG_PICO_CYCLE + 2 * i], clamp16(axes[i].position_rad() * scale));
        put16(&p[REG_PICO_VOLT - REG_PICO_CYCLE + 2 * i], clamp16(axes[i].voltage() * 1000.0f));
        if (axes[i].limit())     limits |= (uint8_t)(1u << (2 * i));
        if (axes[i].end_limit()) limits |= (uint8_t)(1u << (2 * i + 1));
    }
    put16(&p[REG_PICO_TEMP - REG_PICO_CYCLE], clamp16(rp2040_temperature() * 10.0f));
    for (int b = 0; b < 4; ++b) p[REG_PICO_LOOP - REG_PICO_CYCLE + b] = (uint8_t)(loop_count_ >> (8 * b));
    p[REG_PICO_LIMITS - REG_PICO_CYCLE] = limits;
    bus.write(STM32_I2C_ADDR, blk, sizeof(blk));
}
