#pragma once
// Un eje (articulación) del SCARA: encoder + driver + final de carrera + secuencia de home.
// Unidades: ángulos en radianes, entrada del motor en voltios.
#include <cstdint>
#include "axis_config.h"
#include "encoder.h"
#include "motor_driver.h"
#include "limit_switch.h"

enum class AxisState : uint8_t { Disabled, Homing, Ready, Fault };
enum class HomingPhase : uint8_t { Search, GoTo };

class Axis {
public:
    explicit Axis(const AxisConfig& cfg);

    void init();      // inicializa el hardware (el motor queda deshabilitado)
    void update();    // un ciclo de control; lo llama el núcleo 1 cada Ts. Es lo único que mueve el motor

    // Órdenes (las da el núcleo 0)
    void start_homing() { home_request_ = true; }
    void set_target_rad(float rad);   // ángulo objetivo de la articulación, recortado al rango permitido

    void trip() { trip_request_ = true; }   // el STM32 cortó este motor: el eje pasa a Fault

    // Lecturas (seguras desde cualquier núcleo)
    AxisState state() const {
        if (trip_request_) return AxisState::Fault;
        return home_request_ ? AxisState::Homing : state_;
    }
    HomingPhase homing_phase() const { return phase_; }
    bool limit() const { return limit_.pressed(); }        // final de carrera de home
    bool end_limit() const { return end_.pressed(); }      // final de carrera del otro extremo
    bool wants_enable() const { return driver_.enabled(); } // lo que hay que pedirle al STM32
    float position_rad() const { return position_rad_; }   // ángulo de la articulación
    float target_rad() const { return target_rad_; }
    float voltage() const { return u_volts_; }              // último voltaje mandado al motor (RST o home)
    const char* name() const { return cfg_.name; }

private:
    void begin_homing();
    void homing_step();
    void zero_here();   // el punto actual pasa a ser 0 y empieza la fase GoTo
    void drive(float volts);   // manda un voltaje al motor y lo guarda para la telemetría
    void stop_fault();  // PWM a 0, deja de pedir el EN y queda en Fault

    const AxisConfig& cfg_;
    Encoder encoder_;
    MotorDriver driver_;
    LimitSwitch limit_;
    LimitSwitch end_;
    RstController rst_;

    // Solo los usa el núcleo 1
    float motor_rad_ = 0.0f;      // ángulo del eje del motor (el que usará el RST)
    float dir_ = 1.0f;            // sentido del movimiento hacia el ángulo objetivo
    uint32_t ticks_ = 0;          // ciclos desde que empezó el home (para el timeout)
    uint32_t pause_ticks_ = 0;    // ciclos que faltan de pausa antes de ir al objetivo

    // Los escribe el núcleo 1 y los lee el núcleo 0
    volatile float position_rad_ = 0.0f;
    volatile AxisState state_ = AxisState::Disabled;
    volatile HomingPhase phase_ = HomingPhase::Search;
    volatile float target_rad_ = 0.0f;   // lo escribe el núcleo 0 y lo lee el núcleo 1
    volatile float u_volts_ = 0.0f;

    // Lo escribe el núcleo 0 (pide) y lo borra el núcleo 1 (atiende)
    volatile bool home_request_ = false;
    volatile bool trip_request_ = false;   // lo escribe el núcleo 0; el núcleo 1 lo atiende y queda en Fault
};
