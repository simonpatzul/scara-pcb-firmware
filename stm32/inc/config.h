#pragma once
// Parámetros del STM32 de protección. Todo lo ajustable vive aquí.

// ---------- Protección de corriente (valores por defecto; la Pico los puede cambiar por I2C) ----------
#define I_SUST_MA        3000   // corriente sostenida máxima
#define T_SUST_MS        500    // tiempo por encima de I_SUST_MA antes de cortar
#define I_PEAK_MA        4500   // corriente de pico máxima (el motor se traba a 5.5 A)
#define T_PEAK_MS        20     // tiempo por encima de I_PEAK_MA antes de cortar

// El IFX007T, en falla (sobretemperatura, corto), saca por IS una corriente fija de varios mA:
// con R_IS = 470 ohm son ~2 V. Una corriente de motor normal da < 0.2 V.
#define DRIVER_FAULT_MV  1500
#define T_DRIVER_FAULT_MS 5

#define HEARTBEAT_TIMEOUT_MS 100  // si la Pico no escribe REG_CONTROL en este tiempo, se apagan los EN

// ---------- Sensado de corriente ----------
// I_motor = (V_IS / R_IS) * K_ILIS.  K_ILIS es el valor típico de la familia NovalithIC:
// VERIFICAR con la hoja de datos del IFX007T y ajustar midiendo con un multímetro.
#define K_ILIS           20000.0f
#define R_IS_OHM         470.0f
#define CAL_MS           200    // al arrancar, con los EN apagados, se mide el cero de cada canal

// ---------- Telemetría ----------
#define TELEMETRY_PERIOD_MS 500
