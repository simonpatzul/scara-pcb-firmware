#pragma once
// Puente H con una entrada PWM por sentido (pwm_l, pwm_r) y un enable por mitad.
// Recibe un "duty" normalizado de -1 a 1: el signo elige el sentido, el valor absoluto es el PWM.
#include <cstdint>
#include "pico/types.h"

class MotorDriver {
public:
    MotorDriver(uint pwm_l, uint pwm_r, uint en_l, uint en_r, float max_duty);

    void init();            // configura pines. Termina con el driver deshabilitado y PWM en 0
    void enable(bool on);   // habilita o deshabilita el puente
    void set(float duty);   // -1..1, recortado a max_duty. Positivo = PWM_R, negativo = PWM_L

private:
    uint pwm_l_, pwm_r_, en_l_, en_r_;
    float max_duty_;
    uint slice_ = 0;
    uint chan_l_ = 0, chan_r_ = 0;
    uint16_t wrap_ = 0;
};
