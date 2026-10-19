// Programa aparte para caracterizar el eje prismático en lazo abierto.
// Al dar Enter el motor arranca con VOLTS (lo cambias aquí abajo) y se apaga si el límite se activa.
// Cada 100 ms imprime: voltaje, cuentas, vueltas del motor y estado del límite.
#include <cstdio>
#include "pico/stdlib.h"
#include "encoder.h"
#include "motor_driver.h"
#include "limit_switch.h"

constexpr float VOLTS          = -0.0f;      // voltaje de la prueba. El signo es el sentido
constexpr float SUPPLY_VOLTS   = 12.0f;
constexpr float COUNTS_PER_REV = 3200.0f;   // supuesto: mismo Pololu #4753 que las revolutas

Encoder     encoder(pio0, 0, 11, 10);          // A = GP11, B = GP10
MotorDriver driver(0, 1, 2, 3, 1.0f);        // PWM_L, PWM_R, EN_L, EN_R, sin tope de duty
LimitSwitch limit(19, true);                 // activo en bajo (supuesto, igual que las revolutas)

int main() {
    stdio_init_all();
    driver.init();
    encoder.init();
    limit.init();

    sleep_ms(3000);   // tiempo para abrir el monitor serie
    printf("\nPrueba prismatico: %.2f V. Presiona ENTER para arrancar.\n", VOLTS);
    int c;
    do { c = getchar(); } while (c != '\n' && c != '\r');

    float volts = VOLTS;
    driver.enable(true);
    driver.set(volts / SUPPLY_VOLTS);

    while (true) {
        const bool pressed = limit.pressed();
        if (pressed && volts != 0.0f) {      // seguridad: al tocar el límite se apaga
            volts = 0.0f;
            driver.set(0.0f);
        }
        const int32_t cnt = encoder.count();
        printf("V %6.2f | cuentas %8ld | vueltas motor %9.3f | limite=%d | A(GP8)=%d B(GP9)=%d\n",
               volts, (long)cnt, cnt / COUNTS_PER_REV, pressed, gpio_get(8), gpio_get(9));
        sleep_ms(100);
    }
}
