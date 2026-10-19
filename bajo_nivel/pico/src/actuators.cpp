#include "actuators.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

namespace {
constexpr uint SERVO_PIN = 8;
constexpr uint PUMP_PIN = 9;
constexpr uint16_t WRAP = 19999;          // 20000 ticks de 1 us = 20 ms = 50 Hz
constexpr float SERVO_MIN_US = 1000.0f;   // pulso a 0°. Ajustar al servo que usen (muchos van de 500 a 2500)
constexpr float SERVO_MAX_US = 2000.0f;   // pulso a 180°
uint slice;
float servo_now = 90.0f, pump_now = 0.0f;
}

namespace actuators {

void init() {
    slice = pwm_gpio_to_slice_num(SERVO_PIN);   // la misma que PUMP_PIN
    pwm_set_clkdiv(slice, clock_get_hz(clk_sys) / 1000000.0f);   // 1 tick = 1 us
    pwm_set_wrap(slice, WRAP);
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);
    gpio_set_function(PUMP_PIN, GPIO_FUNC_PWM);
    servo_deg(90.0f);
    pump(0.0f);
    pwm_set_enabled(slice, true);
}

void servo_deg(float deg) {
    if (!(deg >= 0.0f)) deg = 0.0f;   // también atrapa NaN
    if (deg > 180.0f) deg = 180.0f;
    servo_now = deg;
    const float us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * deg / 180.0f;
    pwm_set_gpio_level(SERVO_PIN, (uint16_t)us);
}

void pump(float level) {
    if (!(level >= 0.0f)) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    pump_now = level;
    pwm_set_gpio_level(PUMP_PIN, (uint16_t)(level * (WRAP + 1)));
}

float servo() { return servo_now; }
float pump_level() { return pump_now; }

}  // namespace actuators
