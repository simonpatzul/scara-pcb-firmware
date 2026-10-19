#include "limit_switch.h"
#include "pico/stdlib.h"
 
void LimitSwitch::init() {
    gpio_init(pin_);
    gpio_set_dir(pin_, GPIO_IN);
    if (active_low_) gpio_pull_up(pin_);
    else             gpio_pull_down(pin_);
}
 
bool LimitSwitch::pressed() const {
    return gpio_get(pin_) != active_low_;
}
 