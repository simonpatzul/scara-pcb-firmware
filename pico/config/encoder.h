#pragma once
// Encoder en cuadratura leído por una máquina de estados (SM) de la PIO.
// La PIO cuenta los flancos de A y B en hardware; la CPU solo lee el conteo.
// A y B deben estar en GPIO consecutivos. Un bloque PIO admite hasta 4 encoders.
#include <cstdint>
#include "hardware/pio.h"

class Encoder {
public:
    Encoder(PIO pio, uint sm, uint pin_a, uint pin_b);

    void init();             // configura pines y arranca la SM
    int32_t count() const;   // cuentas desde el último reset()
    void reset();            // la posición actual pasa a ser 0

private:
    int32_t raw() const;

    PIO pio_;
    uint sm_;
    uint pin_a_, pin_b_;
    int32_t offset_ = 0;
};
