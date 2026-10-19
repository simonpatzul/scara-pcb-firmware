#include "encoder.h"
#include "pico/stdlib.h"
#include "quadrature_encoder.pio.h"   // generado a partir del .pio al compilar

// El programa PIO debe cargarse una sola vez por bloque, en la dirección 0.
static bool program_loaded[NUM_PIOS] = {};

Encoder::Encoder(PIO pio, uint sm, uint pin_a, uint pin_b)
    : pio_(pio), sm_(sm), pin_a_(pin_a), pin_b_(pin_b) {}

void Encoder::init() {
    const uint pin_base = pin_a_ < pin_b_ ? pin_a_ : pin_b_;

    const uint idx = pio_get_index(pio_);
    if (!program_loaded[idx]) {
        pio_add_program_at_offset(pio_, &quadrature_encoder_program, 0);
        program_loaded[idx] = true;
    }

    pio_sm_claim(pio_, sm_);
    quadrature_encoder_program_init(pio_, sm_, pin_base, 0);  // 0 = máxima velocidad de muestreo
    offset_ = raw();
}

int32_t Encoder::raw() const {
    return quadrature_encoder_get_count(pio_, sm_);
}

int32_t Encoder::count() const {
    return raw() - offset_;
}

void Encoder::reset() {
    offset_ = raw();
}
