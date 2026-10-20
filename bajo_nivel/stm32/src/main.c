// STM32F030C8T6 de protección del SCARA RRP.
//
//   SysTick (1 kHz)  -> protection_tick(): mide corriente, corta los EN por sobrecorriente,
//                       falla del driver o pérdida del latido de la Pico.
//   I2C1 (interrup.) -> la Pico pide encender los EN, borra fallas y manda su telemetría.
//   Bucle principal  -> FC41D (Wi-Fi + MQTT) y watchdog.
//
// Si el programa se cuelga, el IWDG reinicia el chip en ~100 ms; al reiniciar, los pines quedan como
// entrada y las resistencias de 10k de la placa bajan los EN: los motores se apagan solos.
#include "board.h"
#include "fc41d.h"
#include "i2c_slave.h"
#include "protection.h"
#include "telemetry.h"

static volatile bool started = false;

void SysTick_Handler(void) {
    ms_ticks++;
    if (started) protection_tick();
}

int main(void) {
    board_init();          // deja los EN en bajo antes que nada
    protection_init();
    i2c_slave_init();
    fc41d_init();
    started = true;
    watchdog_start();

    uint32_t last_tick = 0;
    while (1) {
        // Solo se alimenta el watchdog si el SysTick (y con él la protección) sigue corriendo
        if (ms_ticks != last_tick) {
            last_tick = ms_ticks;
            watchdog_kick();
        }
        i2c_slave_poll();
        fc41d_poll();
        telemetry_poll();
    }
}
