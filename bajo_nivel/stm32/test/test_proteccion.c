// Prueba en el PC de protection.c y telemetry.c:  make -C stm32/test
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "board.h"
#include "config.h"
#include "shared.h"
#include "protection.h"
#include "telemetry.h"

volatile uint16_t adc_buf[ADC_SCANS][NUM_ADC_CH];
volatile uint32_t ms_ticks = 0;
volatile uint8_t regs[REG_COUNT];
volatile uint32_t pico_last_block_ms = 0;
volatile bool wifi_ok = true, mqtt_ok = true;
bool en_pin[NUM_MOTORS];

static char last_topic[64], last_msg[700];
bool fc41d_ready(void) { return true; }
bool fc41d_publish(const char* t, const char* p, int q) { (void)q; strcpy(last_topic, t); strcpy(last_msg, p); return true; }

// Llena el buffer del ADC: el canal ch con `on` cuentas en la fracción `duty` de las muestras, el resto con `off`
static void fill(int ch, int off, int on, float duty) {
    for (int s = 0; s < ADC_SCANS; ++s) adc_buf[s][ch] = (s < duty * ADC_SCANS) ? on : off;
}
static void run(int ms) { for (int i = 0; i < ms; ++i) { ms_ticks++; protection_tick(); } }
static void heartbeat(uint8_t en, int ms) { for (int i = 0; i < ms; ++i) { if (i % 20 == 0) protection_on_control(en); run(1); } }

// Cuentas del ADC para una corriente de motor (A) según config.h
static int amps(float a) { return (int)(a / K_ILIS * R_IS_OHM * 1000.0f / (3300.0f / 4095.0f) + 0.5f); }

// Telemetría de la Pico: voltaje mandado a cada motor (mV); también la marca como reciente
static void pico_volts(int16_t u0, int16_t u1, int16_t u2) {
    put16(REG_PICO_VOLT, u0); put16(REG_PICO_VOLT + 2, u1); put16(REG_PICO_VOLT + 4, u2);
    pico_last_block_ms = ms_ticks;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const int OFS = 99;   // offset típico del IS con el EN encendido (170 uA x 470 ohm)
    for (int c = 0; c < NUM_ADC_CH; ++c) fill(c, 0, 0, 0);   // con el EN apagado el IS da 0
    protection_init();

    // 1. Durante el arranque no se enciende nada aunque la Pico lo pida
    heartbeat(0x07, 30);
    assert(!en_pin[0] && !en_pin[1] && !en_pin[2]);
    heartbeat(0x07, 40);
    assert(en_pin[0] && en_pin[1] && en_pin[2]);
    assert(regs[REG_STATUS] & 0x08);
    printf("ok  arranque y enable\n");

    // Con el EN encendido aparece el offset en los 6 canales. Motores quietos: se aprende el offset
    for (int c = 0; c < NUM_ADC_CH; ++c) fill(c, OFS, OFS, 0);
    pico_volts(0, 0, 0);
    heartbeat(0x07, 300);
    assert(get16(REG_CURRENT) < 100 && get16(REG_CURRENT + 2) < 100);
    printf("ok  offset aprendido, corriente en reposo %d mA\n", get16(REG_CURRENT));

    // 2. 2 A en M1 con PWM al 50 %: no corta, y reporta ~2 A (no 2 A + offset)
    pico_volts(6000, 0, 0);
    fill(0, OFS, OFS + amps(2.0f), 0.5f);
    heartbeat(0x07, 1000);
    assert(en_pin[0]);
    const int16_t ma = get16(REG_CURRENT);
    printf("ok  2 A sin corte (medido %d mA)\n", ma);
    assert(ma > 1850 && ma < 2150);

    // El mismo motor al 100 % (sin parte "off" en la ventana): se mide contra el offset aprendido
    fill(0, OFS, OFS + amps(2.0f), 1.0f);
    heartbeat(0x07, 200);
    printf("ok  2 A al 100 %% de PWM (medido %d mA)\n", get16(REG_CURRENT));
    assert(get16(REG_CURRENT) > 1850 && get16(REG_CURRENT) < 2150);
    fill(0, OFS, OFS, 0);

    // 3. 3.5 A sostenido en M2 (canal 3, el otro sentido): corta a los 500 ms, no antes
    pico_volts(0, -3000, 0);
    fill(3, OFS, OFS + amps(3.5f), 0.3f);
    heartbeat(0x07, 450);
    assert(en_pin[1]);
    heartbeat(0x07, 100);
    assert(!en_pin[1] && regs[REG_CAUSE + 1] == FAULT_SUSTAINED && (regs[REG_FAULT] & 0x02));
    printf("ok  sostenida en M2 corta entre 450 y 550 ms\n");

    // La falla queda enclavada aunque la corriente baje, hasta que la Pico la borre
    fill(3, OFS, OFS, 0);
    heartbeat(0x07, 50);
    assert(!en_pin[1]);
    protection_on_clear(0x02);
    heartbeat(0x07, 5);
    assert(en_pin[1] && regs[REG_CAUSE + 1] == FAULT_NONE);
    printf("ok  falla enclavada y borrado\n");

    // 4. 5 A en M3: corta por pico en ~20 ms
    pico_volts(0, 0, 9000);
    fill(4, OFS, OFS + amps(5.0f), 0.5f);
    heartbeat(0x07, 25);
    assert(!en_pin[2] && regs[REG_CAUSE + 2] == FAULT_PEAK);
    fill(4, OFS, OFS, 0);
    printf("ok  pico en M3\n");

    // 5. Nivel de falla del driver (IS = 4.1 mA x 470 ohm = 1.93 V, el mínimo del datasheet) en M1
    fill(1, 2395, 2395, 1.0f);
    heartbeat(0x07, 10);
    assert(!en_pin[0] && regs[REG_CAUSE + 0] == FAULT_DRIVER);
    fill(1, OFS, OFS, 0);
    printf("ok  falla del driver en M1\n");

    // 3 A + el offset máximo del datasheet (385 uA) NO debe parecer falla del driver
    assert((OFS + amps(4.5f)) * 3300 / 4095 < DRIVER_FAULT_MV);

    // 6. La Pico deja de mandar el latido: se apagan los EN y queda la causa "enlace"
    protection_on_clear(0x07 | 0x80);
    heartbeat(0x07, 50);
    assert(en_pin[0] && en_pin[1] && en_pin[2]);
    run(150);
    assert(!en_pin[0] && !en_pin[1] && !en_pin[2]);
    assert(regs[REG_CAUSE + 0] == FAULT_LINK && (regs[REG_STATUS] & 0x40));
    printf("ok  hombre muerto (latido perdido)\n");

    // 7. Umbrales configurables, con valores absurdos ignorados
    put16(REG_CFG_I_SUST, 0);
    protection_on_config();
    assert(get16(REG_CFG_I_SUST) == 3000);
    put16(REG_CFG_I_SUST, 2500);
    protection_on_config();
    assert(get16(REG_CFG_I_SUST) == 2500);
    printf("ok  configuracion\n");

    // 8. JSON de telemetría
    regs[REG_PICO_CYCLE] = CYCLE_READY;
    regs[REG_PICO_STATE] = 'R'; regs[REG_PICO_STATE + 1] = 'R'; regs[REG_PICO_STATE + 2] = 'H';
    put16(REG_PICO_POS, 512); put16(REG_PICO_POS + 2, -200); put16(REG_PICO_POS + 4, 3512);
    put16(REG_PICO_VOLT, 3200); put16(REG_PICO_VOLT + 2, -1100); put16(REG_PICO_VOLT + 4, -8000);
    put16(REG_PICO_TEMP, 345);
    regs[REG_PICO_LIMITS] = 0x10;
    pico_last_block_ms = ms_ticks;
    event_mask = 0;
    ms_ticks += 1000;
    pico_last_block_ms = ms_ticks;
    telemetry_poll();
    printf("%s %s\n", last_topic, last_msg);
    assert(strcmp(last_topic, "scara/estado") == 0);
    assert(strstr(last_msg, "\"ciclo\":\"LISTO\""));
    assert(strstr(last_msg, "\"pos\":-0.200"));
    assert(strstr(last_msg, "\"u\":-11.00") == NULL && strstr(last_msg, "\"u\":-1.10"));
    assert(strstr(last_msg, "\"lim\":[1,0]"));
    printf("ok  json\n");
    return 0;
}
