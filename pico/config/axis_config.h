#pragma once
// Todo lo que cambia de un motor a otro vive aquí, y solo aquí.
// Para replicar en otro eje: copias el bloque y cambias los números.
#include <cstdint>
#include "rst_controller.h"

struct AxisConfig {
    const char* name;

    // Encoder
    uint8_t enc_a, enc_b;      // GPIO del encoder (consecutivos)
    uint8_t pio_sm;            // máquina de estados PIO que lo lee (0..3)
    float counts_per_rev;      // cuentas por vuelta del eje del motor
    float gear_ratio;          // rad del motor por unidad de la articulación: 3.0 en R1/R2 (3:1), pi en P (rad/mm, tornillo de 2 mm)
    float encoder_sign;        // +1 o -1. Debe quedar tal que voltaje POSITIVO => ángulo que AUMENTA

    // Driver (puente H)
    uint8_t pwm_l, pwm_r;      // PWM de cada sentido (misma pareja PWM)
    uint8_t en_l, en_r;        // enable de cada mitad del puente
    float supply_volts;        // voltaje de la fuente del motor
    float max_duty;            // tope del PWM, de 0 a 1. También satura al controlador (u_max = max_duty * supply_volts)

    // Final de carrera (uno solo, el de home)
    uint8_t limit_pin;
    bool limit_active_low;     // true si el interruptor conecta el pin a GND al activarse

    // Home
    float home_volts;          // voltaje de la búsqueda. Su SIGNO es el sentido hacia el límite
    float home_target_rad;     // desplazamiento (articulación, rad) desde el límite hasta el cero
    uint32_t home_timeout_ms;  // tiempo máximo de todo el home; si se pasa, el eje entra en falla

    // Control
    const RstParams* rst;      // coeficientes del RST de este motor
    float angle_min_rad;       // rango permitido para el objetivo (articulación, rad)
    float angle_max_rad;
};

constexpr uint32_t CONTROL_PERIOD_US = 10000;  // Ts = 10 ms, el mismo de la identificación

// RST del Pololu #4753, Ts = 10 ms, ángulo del motor en rad, u en voltios (control/rst_motor_v3.m).
// Modelo de diseño: Kv / (s (tau s + 1)) con Kv = 1.8 rad/s/V (tu identificación) y tau = 0.04 s
// (lo que muestran los datos del home en el robot). Polos: doble en wn = 6 rad/s, auxiliares en 0.7.
// Los ceros al final son relleno (no cambian nada).
constexpr RstParams RST_R1 = {
    { 2.059447617952, -1.561290298166, -2.047948810692, 1.572789105426, 0.0 },
    { 1.0, -1.208998870792, 0.394471201417, 0.0 },
    { 251.156203565986, -1000.488037304793, 1585.381209607035, -1249.331488730012, 489.710270900887, -76.405160424582, 0.0, 0.0 },
    { 1.0, -1.902459, 0.904837 },
    0.002378
};

//                              name  encA encB sm  cuentas  ratio  sign  pwmL pwmR enL enR  volts  maxDuty  limite activeLow  homeV  objetivo timeout   rst      min   max
constexpr AxisConfig AXIS_R1 = { "R1", 11, 10, 0, 3200.0f, 3.0f, -1.0f,  6,   7,   5,  4, 12.0f, 0.50f,    21,    true,   -3.5f,  1.83f, 15000, &RST_R1, -1.885f, 1.885f };
constexpr AxisConfig AXIS_R2 = { "R2", 12, 13, 1, 3200.0f, 3.0f, 1.0f,  14,  15,  17, 16, 12.0f, 0.50f,    20,    true,   -3.5f,  1.48f, 15000, &RST_R1, -1.57f, 1.57f };

// Prismático: sus "rad" son mm (0 = suelo, positivo hacia arriba). Límite abajo, en el suelo.
// pwmL/pwmR van cruzados (1, 0) para que voltaje positivo = subir.
constexpr AxisConfig AXIS_P  = { "P",   8,  9, 2, 3200.0f, 3.14159265f, -1.0f, 1, 0, 2,  3, 12.0f, 1.00f,    19,    true,   -8.0f,  0.0f,  30000, &RST_R1, 0.0f, 85.0f };
