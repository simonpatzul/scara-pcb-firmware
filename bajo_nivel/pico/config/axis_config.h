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

    // Driver (dos medios puentes IFX007T). El EN (INH) no lo maneja la Pico: lo enciende el STM32
    // de protección cuando la Pico se lo pide por I2C (ver stm32_link.h)
    uint8_t pwm_l, pwm_r;      // PWM de cada sentido: M*_INA / M*_INB (misma pareja PWM)
    float supply_volts;        // voltaje de la fuente del motor
    float max_duty;            // tope del PWM, de 0 a 1. También satura al controlador (u_max = max_duty * supply_volts)

    // Finales de carrera: dos por eje, en orden LIMIT1..LIMIT6 = GP17..GP22
    uint8_t limit_home_pin;    // el de home (hacia donde empuja home_volts)
    uint8_t limit_end_pin;     // el del otro extremo
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

// Pines de la PCB (MCU.SchDoc): M1 = R1, M2 = R2, M3 = P.
//   M1_INA/INB GP0/GP1   M2_INA/INB GP2/GP3   M3_INA/INB GP6/GP7
//   ENC1 GP10/GP11       ENC2 GP12/GP13       ENC3 GP14/GP15     (A/B, ya pasados a 3.3 V por el SN74LVC8T245)
//   LIMIT1/2 GP17/GP18   LIMIT3/4 GP19/GP20   LIMIT5/6 GP21/GP22 (activos en bajo, pull-up de 10k en la placa)
//
// VERIFICAR antes de cerrar el lazo, con el programa scara_prueba (ver README):
//   - encoder_sign: R1 pasó de -1 a +1 porque en la PCB ENC1_A quedó en GP10 (en la baquelita estaba en GP11).
//     Si el motor quedó conectado al revés en los bornes, el signo vuelve a cambiar.
//   - pwmL/pwmR de P siguen cruzados como en la baquelita (voltaje positivo = subir).
//
//                              name  encA encB sm  cuentas  ratio  sign  pwmL pwmR  volts  maxDuty  home end activeLow  homeV  objetivo timeout   rst      min   max
constexpr AxisConfig AXIS_R1 = { "R1", 10, 11, 0, 3200.0f, 3.0f,  1.0f,  0,   1,  12.0f, 0.50f,    17,  18,   true,   -3.5f,  1.83f, 15000, &RST_R1, -1.885f, 1.885f };
constexpr AxisConfig AXIS_R2 = { "R2", 12, 13, 1, 3200.0f, 3.0f,  1.0f,  2,   3,  12.0f, 0.50f,    19,  20,   true,   -3.5f,  1.48f, 15000, &RST_R1, -1.57f, 1.57f };

// Prismático: sus "rad" son mm (0 = suelo, positivo hacia arriba). Límite de home abajo, en el suelo.
// pwmL/pwmR van cruzados (7, 6) para que voltaje positivo = subir.
constexpr AxisConfig AXIS_P  = { "P",  14, 15, 2, 3200.0f, 3.14159265f, -1.0f, 7, 6, 12.0f, 1.00f,   21,  22,   true,   -8.0f,  0.0f,  30000, &RST_R1, 0.0f, 85.0f };
