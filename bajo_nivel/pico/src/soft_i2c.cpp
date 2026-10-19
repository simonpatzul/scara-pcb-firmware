#include "soft_i2c.h"
#include <initializer_list>
#include "pico/stdlib.h"

constexpr uint32_t HALF_US = 5;             // medio periodo: ~100 kHz
constexpr uint32_t STRETCH_TIMEOUT_US = 1000;

// Open drain: 0 = pin en salida a bajo, 1 = pin en entrada (lo sube el pull-up de 5.1k de la placa)
void SoftI2C::sda(bool high) { gpio_set_dir(sda_, high ? GPIO_IN : GPIO_OUT); }

bool SoftI2C::scl(bool high) {
    gpio_set_dir(scl_, high ? GPIO_IN : GPIO_OUT);
    if (!high) return true;
    const absolute_time_t limit = make_timeout_time_us(STRETCH_TIMEOUT_US);
    while (!gpio_get(scl_)) {
        if (time_reached(limit)) return false;
    }
    return true;
}

void SoftI2C::init() {
    for (uint pin : {sda_, scl_}) {
        gpio_init(pin);
        gpio_put(pin, 0);          // cuando el pin sea salida, será un 0
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);         // refuerzo; los pull-ups reales están en la placa
    }
    recover();
}

void SoftI2C::recover() {
    for (int i = 0; i < 9 && !gpio_get(sda_); ++i) {
        scl(false); busy_wait_us(HALF_US);
        scl(true);  busy_wait_us(HALF_US);
    }
    stop();
}

void SoftI2C::start() {
    sda(true); scl(true); busy_wait_us(HALF_US);
    sda(false); busy_wait_us(HALF_US);
    scl(false);
}

void SoftI2C::stop() {
    sda(false); busy_wait_us(HALF_US);
    scl(true); busy_wait_us(HALF_US);
    sda(true); busy_wait_us(HALF_US);
}

bool SoftI2C::write_byte(uint8_t b) {
    for (int i = 7; i >= 0; --i) {
        sda((b >> i) & 1);
        busy_wait_us(HALF_US);
        if (!scl(true)) return false;
        busy_wait_us(HALF_US);
        scl(false);
    }
    sda(true);                       // soltar SDA para leer el ACK
    busy_wait_us(HALF_US);
    if (!scl(true)) return false;
    const bool ack = !gpio_get(sda_);
    busy_wait_us(HALF_US);
    scl(false);
    return ack;
}

uint8_t SoftI2C::read_byte(bool ack) {
    uint8_t b = 0;
    sda(true);
    for (int i = 0; i < 8; ++i) {
        busy_wait_us(HALF_US);
        scl(true);
        b = (uint8_t)((b << 1) | (gpio_get(sda_) ? 1 : 0));
        busy_wait_us(HALF_US);
        scl(false);
    }
    sda(!ack);                       // ACK = SDA en bajo; el último byte va con NACK
    busy_wait_us(HALF_US);
    scl(true);
    busy_wait_us(HALF_US);
    scl(false);
    sda(true);
    return b;
}

bool SoftI2C::write(uint8_t addr, const uint8_t* data, size_t n) {
    start();
    bool ok = write_byte((uint8_t)(addr << 1));
    for (size_t i = 0; ok && i < n; ++i) ok = write_byte(data[i]);
    stop();
    return ok;
}

bool SoftI2C::read_reg(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
    start();
    bool ok = write_byte((uint8_t)(addr << 1)) && write_byte(reg);
    if (ok) {
        start();                     // start repetido
        ok = write_byte((uint8_t)((addr << 1) | 1));
        for (size_t i = 0; ok && i < n; ++i) buf[i] = read_byte(i + 1 < n);
    }
    stop();
    return ok;
}
