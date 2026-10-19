#pragma once
// Final de carrera conectado a un GPIO.
// active_low = true: el interruptor conecta el pin a GND al activarse (usa pull-up interno).
// active_low = false: el interruptor conecta el pin a 3.3 V al activarse (usa pull-down interno).
#include "pico/types.h"
 
class LimitSwitch {
public:
    LimitSwitch(uint pin, bool active_low) : pin_(pin), active_low_(active_low) {}
 
    void init();
    bool pressed() const;   // true si el interruptor está activado ahora mismo
 
private:
    uint pin_;
    bool active_low_;
};
 