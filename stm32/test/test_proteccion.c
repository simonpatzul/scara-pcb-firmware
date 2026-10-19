// Prueba en el PC de protection.c y telemetry.c:  make -C stm32/test
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "board.h"
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

int main(void) {
    const int OFS = 20;   // offset del ADC en reposo
    for (int c = 0; c < NUM_ADC_CH; ++c) fill(c, OFS, OFS, 0);
    protection_init();

    // 1. Durante la calibración no se enciende nada aunque la Pico lo pida
    heartbeat(0x07, 100);
    assert(!en_pin[0] && !en_pin[1] && !en_pin[2]);
    heartbeat(0x07, 200);
    assert(en_pin[0] && en_pin[1] && en_pin[2]);
    assert(regs[REG_STATUS] & 0x08);
    printf("ok  calibracion y enable\n");

    // 2. 2 A en M1 (≈58 cuentas) con PWM al 50 %: no corta, y reporta ~2 A
    fill(0, OFS, OFS + 58, 0.5f);
    heartbeat(0x07, 1000);
    assert(en_pin[0]);
    const int16_t ma = get16(REG_CURRENT);
    printf("ok  2 A sin corte (medido %d mA)\n", ma);
    assert(ma > 1800 && ma < 2200);

    // 3. 3.5 A sostenido en M2 (canal 3, el otro sentido): corta a los 500 ms, no antes
    fill(3, OFS, OFS + 102, 0.3f);
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
    fill(4, OFS, OFS + 146, 0.5f);
    heartbeat(0x07, 25);
    assert(!en_pin[2] && regs[REG_CAUSE + 2] == FAULT_PEAK);
    fill(4, OFS, OFS, 0);
    printf("ok  pico en M3\n");

    // 5. Nivel de falla del driver (~2.1 V fijo) en M1
    fill(1, 2600, 2600, 1.0f);
    heartbeat(0x07, 10);
    assert(!en_pin[0] && regs[REG_CAUSE + 0] == FAULT_DRIVER);
    fill(1, OFS, OFS, 0);
    fill(0, OFS, OFS, 0);
    printf("ok  falla del driver en M1\n");

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
