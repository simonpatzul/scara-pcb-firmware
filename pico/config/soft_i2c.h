#pragma once
// I2C maestro por software (bit-bang), ~100 kHz, con soporte de clock stretching.
//
// Por qué no el I2C de hardware: en la PCB, GP4 quedó como SCL y GP5 como SDA. En el RP2040 el I2C de
// hardware usa los pines pares como SDA y los impares como SCL (GP4 = I2C0 SDA, GP5 = I2C0 SCL), así que
// con este ruteo no se puede usar. Si en una revisión de la PCB se cruzan esas dos pistas, se puede
// cambiar esta clase por hardware/i2c.h sin tocar nada más.
#include <cstddef>
#include <cstdint>
#include "pico/types.h"

class SoftI2C {
public:
    SoftI2C(uint sda, uint scl) : sda_(sda), scl_(scl) {}

    void init();
    bool write(uint8_t addr, const uint8_t* data, size_t n);              // true si el esclavo respondió ACK a todo
    bool read_reg(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n);     // escribe reg y lee n bytes

private:
    void sda(bool high);
    bool scl(bool high);          // al soltar SCL espera a que el esclavo deje de estirar el reloj
    void start();
    void stop();
    bool write_byte(uint8_t b);   // devuelve el ACK
    uint8_t read_byte(bool ack);
    void recover();               // 9 pulsos de reloj si un esclavo quedó sosteniendo SDA

    uint sda_, scl_;
};
