#pragma once
// Enlace con el STM32 de protección por I2C (GP5 = SDA, GP4 = SCL).
//
// Cada 20 ms: le dice al STM32 qué motores deben tener el EN encendido (eso también es el latido: si deja
// de llegar por 100 ms, el STM32 apaga todo) y lee sus fallas y corrientes. Si el STM32 cortó un motor,
// el eje correspondiente pasa a Fault.
// Cada 100 ms: le manda la telemetría de la Pico (ciclo, posiciones, voltajes, temperatura) para MQTT.
//
// service() hay que llamarlo seguido desde el núcleo 0, también mientras se espera el Enter y durante el home.
#include <cstdint>
#include "axis.h"
#include "stm32_regs.h"

class Stm32Link {
public:
    void init();                        // configura el bus, comprueba que el STM32 responda y borra sus fallas
    void service(Axis* axes, int n);    // no hace nada si no han pasado 20 ms desde la última vez
    void service_raw(uint8_t en_mask);  // igual, pero sin ejes: pide el EN de los motores de la máscara (pruebas)

    void set_cycle(uint8_t c) { cycle_ = c; }
    void set_loop_count(uint32_t c) { loop_count_ = c; }

    bool present() const { return present_; }
    bool ok() const { return ok_; }                  // la última transacción funcionó
    uint8_t fault_mask() const { return fault_; }
    uint8_t cause(int m) const { return cause_[m]; }
    int16_t current_mA(int m) const { return current_[m]; }
    static const char* cause_name(uint8_t c);

private:
    bool due();                         // true cada 20 ms
    void exchange(uint8_t en_mask);     // pedido de EN + lectura de fallas y corrientes

    bool present_ = false, ok_ = false;
    uint8_t cycle_ = CYCLE_BOOT;
    uint32_t loop_count_ = 0;
    uint8_t fault_ = 0, cause_[3] = {};
    int16_t current_[3] = {};
    uint32_t last_ms_ = 0, last_block_ms_ = 0;
};

float rp2040_temperature();   // °C, sensor interno del RP2040
