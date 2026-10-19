#include "telemetry.h"
#include <string.h>
#include "board.h"
#include "config.h"
#include "fc41d.h"
#include "shared.h"

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#include "wifi_config.example.h"
#endif

volatile uint8_t event_mask = 0;
volatile uint16_t stm32_vdda_mV = 3300;

static const char* const AXIS_NAME[NUM_MOTORS] = { "R1", "R2", "P" };
static const char* const FAULT_NAME[] = { "ninguna", "sostenida", "pico", "driver", "enlace" };
static const char* const CYCLE_NAME[] = { "ARRANQUE", "ESPERA", "HOME_R1", "HOME_R2", "HOME_P",
                                          "LISTO", "QUIETO", "FALLA" };

// ---------- Armado del JSON sin printf de flotantes (no hay FPU y newlib-nano no los imprime) ----------
static char out[640];
static int len;

static void add(const char* s) {
    const int n = (int)strlen(s);
    if (len + n < (int)sizeof(out)) { memcpy(out + len, s, (size_t)n); len += n; out[len] = '\0'; }
}

static void add_int(int32_t v) {
    char tmp[12];
    int i = 0;
    const bool neg = v < 0;
    uint32_t u = neg ? (uint32_t)(-v) : (uint32_t)v;
    do { tmp[i++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) tmp[i++] = '-';
    char s[12];
    for (int k = 0; k < i; ++k) s[k] = tmp[i - 1 - k];
    s[i] = '\0';
    add(s);
}

// Número con punto fijo: value = 1234, decimals = 3 -> "1.234"
static void add_fixed(int32_t value, int decimals) {
    int32_t scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    if (value < 0) { add("-"); value = -value; }
    add_int(value / scale);
    if (decimals == 0) return;
    add(".");
    int32_t frac = value % scale;
    for (int32_t s = scale / 10; s > 0; s /= 10) { char d[2] = { (char)('0' + frac / s), 0 }; add(d); frac %= s; }
}

static void add_str(const char* s) { add("\""); add(s); add("\""); }

static bool pico_ok(void) { return millis() - pico_last_block_ms < 1000 && pico_last_block_ms != 0; }

static void build_state(void) {
    len = 0;
    out[0] = '\0';
    const bool ok = pico_ok();
    const uint8_t cyc = regs[REG_PICO_CYCLE];

    add("{\"v\":1,\"up\":"); add_int((int32_t)(millis() / 1000));
    add(",\"ciclo\":"); add_str(!ok ? "SIN_PICO" : cyc < sizeof(CYCLE_NAME) / sizeof(CYCLE_NAME[0]) ? CYCLE_NAME[cyc] : "?");
    add(",\"pico_ok\":"); add(ok ? "true" : "false");
    add(",\"loop\":"); add_int((int32_t)get32(REG_PICO_LOOP));
    add(",\"temp\":{\"pico\":"); add_fixed(get16(REG_PICO_TEMP), 1);
    add(",\"stm32\":"); add_fixed(get16(REG_STM32_TEMP), 1);
    add("},\"vdda\":"); add_fixed(stm32_vdda_mV, 3);
    add(",\"ejes\":[");
    const uint8_t status = regs[REG_STATUS], limits = regs[REG_PICO_LIMITS];
    for (int m = 0; m < NUM_MOTORS; ++m) {
        if (m) add(",");
        const char est[2] = { ok && regs[REG_PICO_STATE + m] ? (char)regs[REG_PICO_STATE + m] : '?', 0 };
        const uint8_t cause = regs[REG_CAUSE + m];
        add("{\"n\":"); add_str(AXIS_NAME[m]);
        add(",\"est\":"); add_str(est);
        add(",\"pos\":"); add_fixed(get16(REG_PICO_POS + 2 * m), m < 2 ? 3 : 2);   // mrad -> rad, 0.01 mm -> mm
        add(",\"u\":"); add_fixed(get16(REG_PICO_VOLT + 2 * m) / 10, 2);           // mV -> V con 2 decimales
        add(",\"i\":"); add_fixed(get16(REG_CURRENT + 2 * m) / 10, 2);              // mA -> A con 2 decimales
        add(",\"en\":"); add(status & (1u << m) ? "true" : "false");
        add(",\"lim\":["); add_int((limits >> (2 * m)) & 1); add(","); add_int((limits >> (2 * m + 1)) & 1); add("]");
        add(",\"falla\":"); add_str(cause <= FAULT_LINK ? FAULT_NAME[cause] : "?");
        add("}");
    }
    add("]}");
}

static void build_event(int m) {
    len = 0;
    out[0] = '\0';
    const uint8_t cause = regs[REG_CAUSE + m];
    add("{\"up\":"); add_int((int32_t)(millis() / 1000));
    add(",\"eje\":"); add_str(AXIS_NAME[m]);
    add(",\"falla\":"); add_str(cause <= FAULT_LINK ? FAULT_NAME[cause] : "?");
    add(",\"i\":"); add_fixed(get16(REG_CURRENT + 2 * m) / 10, 2);
    add("}");
}

void telemetry_poll(void) {
    static uint32_t last = 0;
    if (!fc41d_ready()) return;

    // Primero los eventos de falla: son los que importan
    for (int m = 0; m < NUM_MOTORS; ++m) {
        if (event_mask & (1u << m)) {
            build_event(m);
            if (fc41d_publish(MQTT_PREFIX "evento", out, 1)) {
                __disable_irq();
                event_mask &= ~(1u << m);
                __enable_irq();
            }
            return;
        }
    }

    if (millis() - last >= TELEMETRY_PERIOD_MS) {
        last = millis();
        build_state();
        fc41d_publish(MQTT_PREFIX "estado", out, 0);
    }
}
