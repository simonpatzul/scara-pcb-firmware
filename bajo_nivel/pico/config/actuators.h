#pragma once
// Servo (GP8) y bomba de vacío (GP9). Los dos pines son la misma slice PWM (4), así que comparten
// frecuencia: 50 Hz, la que necesita el servo. Para la bomba (MOSFET Q2) eso es suficiente para
// prenderla, apagarla o bajarle la potencia.
#include <cstdint>

namespace actuators {
void init();                  // servo al centro, bomba apagada
void servo_deg(float deg);    // 0..180
void pump(float level);       // 0 = apagada, 1 = a fondo
float servo();
float pump_level();
}
