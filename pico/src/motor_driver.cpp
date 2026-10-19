#include "motor_driver.h"
#include <cmath>
#include <initializer_list>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

constexpr uint32_t PWM_FREQ_HZ = 20000;   // fuera del rango audible

MotorDriver::MotorDriver(uint pwm_l, uint pwm_r, uint en_l, uint en_r, float max_duty)
    : pwm_l_(pwm_l), pwm_r_(pwm_r), en_l_(en_l), en_r_(en_r), max_duty_(max_duty) {}

void MotorDriver::init() {
    // 1. Enables como salida y en bajo: el puente queda deshabilitado
    for (uint pin : {en_l_, en_r_}) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }

    // 2. Los dos pines PWM deben pertenecer a la misma "slice" para compartir frecuencia
    slice_  = pwm_gpio_to_slice_num(pwm_l_);
    chan_l_ = pwm_gpio_to_channel(pwm_l_);
    chan_r_ = pwm_gpio_to_channel(pwm_r_);
    if (pwm_gpio_to_slice_num(pwm_r_) != slice_) {
        panic("MotorDriver: pwm_l y pwm_r deben estar en la misma slice PWM");
    }

    // 3. Frecuencia: el contador sube de 0 a wrap_ y el nivel decide cuánto tiempo está en alto
    wrap_ = clock_get_hz(clk_sys) / PWM_FREQ_HZ - 1;
    pwm_set_wrap(slice_, wrap_);
    pwm_set_chan_level(slice_, chan_l_, 0);
    pwm_set_chan_level(slice_, chan_r_, 0);

    gpio_set_function(pwm_l_, GPIO_FUNC_PWM);
    gpio_set_function(pwm_r_, GPIO_FUNC_PWM);
    pwm_set_enabled(slice_, true);
}

void MotorDriver::enable(bool on) {
    if (!on) set(0.0f);              // primero PWM a 0, después se cierra el enable
    gpio_put(en_l_, on);
    gpio_put(en_r_, on);
}

void MotorDriver::set(float duty) {
    if (std::isnan(duty)) duty = 0.0f;
    if (duty >  max_duty_) duty =  max_duty_;
    if (duty < -max_duty_) duty = -max_duty_;

    const uint16_t level = static_cast<uint16_t>(std::fabs(duty) * wrap_);
    // El canal que se apaga se escribe primero: los dos sentidos nunca quedan activos a la vez
    if (duty >= 0.0f) {
        pwm_set_chan_level(slice_, chan_l_, 0);
        pwm_set_chan_level(slice_, chan_r_, level);
    } else {
        pwm_set_chan_level(slice_, chan_r_, 0);
        pwm_set_chan_level(slice_, chan_l_, level);
    }
}
