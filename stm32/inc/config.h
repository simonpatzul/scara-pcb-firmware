#pragma once
// Parámetros del STM32 de protección. Todo lo ajustable vive aquí.

// ---------- Protección de corriente (valores por defecto; la Pico los puede cambiar por I2C) ----------
#define I_SUST_MA        3000   // corriente sostenida máxima
#define T_SUST_MS        500    // tiempo por encima de I_SUST_MA antes de cortar
#define I_PEAK_MA        4500   // corriente de pico máxima (el motor se traba a 5.5 A)
#define T_PEAK_MS        20     // tiempo por encima de I_PEAK_MA antes de cortar

// El IFX007T, en falla (sobretemperatura, límite de corriente), saca por IS 4.1–6.1 mA:
// con R_IS = 470 ohm son 1.93–2.87 V. Una corriente normal más el offset da < 0.3 V (datasheet tabla 11).
#define DRIVER_FAULT_MV  1700
#define T_DRIVER_FAULT_MS 5

#define HEARTBEAT_TIMEOUT_MS 100  // si la Pico no escribe REG_CONTROL en este tiempo, se apagan los EN

// ---------- Sensado de corriente (IFX007T, datasheet rev 1.0 tabla 11) ----------
// I_motor = dkILIS * (I_IS - I_IS(offset)).  dkILIS = 15 000 / 19 500 / 24 000 (mín/típ/máx): ±23 %,
// conviene ajustarlo midiendo una vez con un multímetro.
// I_IS(offset) = 30..385 uA según el chip (14..181 mV, del orden de la señal de 3 A), y solo aparece con
// INH (EN) encendido: con el EN apagado el IS da 0. Por eso el cero NO se mide al arrancar: se aprende con
// el EN encendido, de las muestras en que ese medio puente tiene el lado alto apagado (ver protection.c).
#define K_ILIS           19500.0f
#define R_IS_OHM         470.0f
#define STARTUP_MS       50     // espera al arrancar antes de aceptar pedidos de EN (ADC estable)
#define SWITCH_COUNTS    25     // si max-min de una ventana pasa de esto, ese medio puente está conmutando:
                                // el mínimo de la ventana es su offset
#define IDLE_MV_PICO     300    // si la Pico manda menos de 0.3 V a un motor, sus dos IS son puro offset

// ---------- Telemetría ----------
#define TELEMETRY_PERIOD_MS 500
