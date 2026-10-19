#include "axis.h"
#include <cmath>

constexpr float TWO_PI_F = 6.28318531f;
constexpr uint32_t PAUSE_TICKS = 20;   // 20 ciclos = 200 ms de pausa antes de invertir el sentido

Axis::Axis(const AxisConfig& cfg)
    : cfg_(cfg),
      encoder_(pio0, cfg.pio_sm, cfg.enc_a, cfg.enc_b),
      driver_(cfg.pwm_l, cfg.pwm_r, cfg.en_l, cfg.en_r, cfg.max_duty),
      limit_(cfg.limit_pin, cfg.limit_active_low),
      rst_(*cfg.rst, cfg.max_duty * cfg.supply_volts) {}   // el RST satura igual que el driver

void Axis::init() {
    driver_.init();    // primero el driver, para que el motor esté apagado cuanto antes
    encoder_.init();
    limit_.init();
}

void Axis::update() {
    // 1. Medir
    motor_rad_    = cfg_.encoder_sign * encoder_.count() * TWO_PI_F / cfg_.counts_per_rev;
    position_rad_ = motor_rad_ / cfg_.gear_ratio;

    // 2. Atender la orden de home que dejó el núcleo 0
    if (home_request_) {
        if (state_ != AxisState::Homing) begin_homing();
        home_request_ = false;   // se borra al final: así state() nunca muestra un estado intermedio
    }

    // 3. Actuar según el estado
    if (state_ == AxisState::Homing) homing_step();
    else if (state_ == AxisState::Ready) {
        // El RST trabaja en el motor: la referencia de la articulación se multiplica por la relación
        const float u = rst_.step(target_rad_ * cfg_.gear_ratio, motor_rad_);   // voltios
        driver_.set(u / cfg_.supply_volts);
        u_volts_ = u;
    }
}

void Axis::set_target_rad(float rad) {
    if (rad < cfg_.angle_min_rad) rad = cfg_.angle_min_rad;
    if (rad > cfg_.angle_max_rad) rad = cfg_.angle_max_rad;
    target_rad_ = rad;
}

// ---------------------------------------------------------------------
// Home: Search (ir al límite) -> el cero queda ahí -> GoTo (ir al ángulo objetivo)
// ---------------------------------------------------------------------

void Axis::begin_homing() {
    driver_.enable(true);
    ticks_ = 0;
    state_ = AxisState::Homing;
    if (limit_.pressed()) {
        zero_here();                                     // ya está sobre el límite: ese es el cero
    } else {
        phase_ = HomingPhase::Search;
        driver_.set(cfg_.home_volts / cfg_.supply_volts);   // el signo de home_volts lleva hacia el límite
    }
}

void Axis::zero_here() {
    driver_.set(0.0f);
    encoder_.reset();
    motor_rad_ = 0.0f;
    position_rad_ = 0.0f;
    pause_ticks_ = PAUSE_TICKS;
    phase_ = HomingPhase::GoTo;
}

void Axis::homing_step() {
    // Protección: el home entero tiene un tiempo máximo
    if (++ticks_ * (CONTROL_PERIOD_US / 1000) > cfg_.home_timeout_ms) {
        driver_.set(0.0f);
        driver_.enable(false);
        state_ = AxisState::Fault;
        return;
    }

    if (phase_ == HomingPhase::Search) {
        if (limit_.pressed()) zero_here();
        return;
    }

    // GoTo: primero la pausa, luego se mueve hacia el objetivo y se detiene al alcanzarlo
    if (pause_ticks_ > 0) {
        if (--pause_ticks_ == 0) {
            dir_ = cfg_.home_target_rad > position_rad_ ? 1.0f : -1.0f;
            driver_.set(dir_ * std::fabs(cfg_.home_volts) / cfg_.supply_volts);
        }
        return;
    }
    if ((cfg_.home_target_rad - position_rad_) * dir_ <= 0.0f) {   // ya llegó o se pasó
        driver_.set(0.0f);
        encoder_.reset();                // este punto pasa a ser el cero definitivo
        motor_rad_ = 0.0f;
        position_rad_ = 0.0f;
        target_rad_ = 0.0f;              // se queda en el cero hasta que llegue una orden
        rst_.reset(0.0f);                // el controlador arranca en reposo en el cero
        state_ = AxisState::Ready;       // desde el próximo ciclo manda el RST
    }
}
