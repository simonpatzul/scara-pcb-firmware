#pragma once
// Puente H hecho con dos medios puentes IFX007T: una entrada PWM por sentido (pwm_l, pwm_r).
// El enable (INH) de los dos medios puentes lo maneja el STM32 de protección, no la Pico:
// enable() solo registra que este motor QUIERE estar encendido, y stm32_link se lo pide al STM32.
// Mientras el driver no está habilitado, set() deja el PWM en 0.
// Recibe un "duty" normalizado de -1 a 1: el signo elige el sentido, el valor absoluto es el PWM.
#include <cstdint>
#include "pico/types.h"

class MotorDriver {
public:
    MotorDriver(uint pwm_l, uint pwm_r, float max_duty);

    void init();            // configura pines. Termina deshabilitado y con PWM en 0
    void enable(bool on);   // pide (o deja de pedir) el EN al STM32
    bool enabled() const { return enabled_; }
    void set(float duty);   // -1..1, recortado a max_duty. Positivo = PWM_R, negativo = PWM_L

private:
    uint pwm_l_, pwm_r_;
    float max_duty_;
    uint slice_ = 0;
    uint chan_l_ = 0, chan_r_ = 0;
    uint16_t wrap_ = 0;
    volatile bool enabled_ = false;   // lo escribe el núcleo 1 y lo lee el núcleo 0
};
