// Programa aparte para probar cada eje en lazo abierto ANTES de usar el firmware principal en la PCB.
// Sirve para comprobar el sentido del motor, el signo del encoder y los finales de carrera.
//
// Por el monitor serie (115200, con Enter o sin él):
//   1 / 2 / 3  elegir el eje (R1, R2, P). Al cambiar, el motor se apaga
//   + / -      subir o bajar el voltaje de a 1 V (con signo: es el sentido)
//   0          voltaje a 0
// Cada 200 ms imprime: eje, voltaje, cuentas, ángulo, finales de carrera y corriente medida por el STM32.
// Si el final de carrera del lado hacia el que se mueve se activa, el voltaje vuelve a 0.
//
// Qué revisar: con voltaje POSITIVO el ángulo (o mm en P) debe AUMENTAR. Si disminuye, cambiar el
// signo de encoder_sign de ese eje en axis_config.h. Con voltaje en el signo de home_volts el eje
// debe ir hacia el final de carrera de home.
#include <cstdio>
#include "pico/stdlib.h"
#include "axis_config.h"
#include "encoder.h"
#include "motor_driver.h"
#include "limit_switch.h"
#include "stm32_link.h"

constexpr float MAX_TEST_VOLTS = 6.0f;
const AxisConfig* const CFG[3] = { &AXIS_R1, &AXIS_R2, &AXIS_P };

Encoder*     enc[3];
MotorDriver* drv[3];
LimitSwitch* lim_home[3];
LimitSwitch* lim_end[3];
Stm32Link link;

int main() {
    stdio_init_all();
    for (int i = 0; i < 3; ++i) {
        const AxisConfig& c = *CFG[i];
        drv[i] = new MotorDriver(c.pwm_l, c.pwm_r, 1.0f);
        enc[i] = new Encoder(pio0, c.pio_sm, c.enc_a, c.enc_b);
        lim_home[i] = new LimitSwitch(c.limit_home_pin, c.limit_active_low);
        lim_end[i] = new LimitSwitch(c.limit_end_pin, c.limit_active_low);
        drv[i]->init();
        enc[i]->init();
        lim_home[i]->init();
        lim_end[i]->init();
    }
    link.init();

    int axis = 0;
    float volts = 0.0f;
    absolute_time_t next_print = get_absolute_time();
    bool said_hello = false;

    while (true) {
        // El STM32 solo enciende el EN del eje elegido, y solo mientras haya voltaje
        link.service_raw(volts != 0.0f ? (uint8_t)(1u << axis) : 0);

        const int ch = getchar_timeout_us(0);
        if (ch >= '1' && ch <= '3') {
            drv[axis]->enable(false);
            axis = ch - '1';
            volts = 0.0f;
        } else if (ch == '+' && volts < MAX_TEST_VOLTS) volts += 1.0f;
        else if (ch == '-' && volts > -MAX_TEST_VOLTS) volts -= 1.0f;
        else if (ch == '0') volts = 0.0f;

        const AxisConfig& c = *CFG[axis];
        const bool home = lim_home[axis]->pressed(), end = lim_end[axis]->pressed();
        // Seguridad: no empujar contra un final de carrera presionado
        if ((home && volts * c.home_volts > 0.0f) || (end && volts * c.home_volts < 0.0f)) volts = 0.0f;

        drv[axis]->enable(volts != 0.0f);
        drv[axis]->set(volts / c.supply_volts);

        if (time_reached(next_print)) {
            next_print = make_timeout_time_ms(200);
            if (!said_hello) {
                printf("\nPrueba de ejes. STM32: %s. Teclas: 1/2/3 eje, +/- voltaje, 0 parar\n",
                       link.present() ? "OK" : "NO RESPONDE");
                said_hello = true;
            }
            const int32_t cnt = enc[axis]->count();
            const float joint = c.encoder_sign * cnt * 6.28318531f / c.counts_per_rev / c.gear_ratio;
            printf("%s | V %5.1f | cuentas %8ld | articulacion %9.4f | home=%d fin=%d | %5d mA%s\n",
                   c.name, volts, (long)cnt, joint, home, end, link.current_mA(axis),
                   link.fault_mask() & (1u << axis) ? " | FALLA STM32" : "");
        }
        sleep_ms(2);
    }
}
