#include <cstdio>
#include <cstdlib>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "axis.h"
#include "stm32_link.h"
#include "actuators.h"

// Los ejes que controla esta Pico. Para agregar uno: su AxisConfig en axis_config.h y una línea aquí.
Axis axes[] = { Axis(AXIS_R1), Axis(AXIS_R2), Axis(AXIS_P) };
constexpr int N_AXES = sizeof(axes) / sizeof(axes[0]);

// ---------- Núcleo 1: solo el lazo de control, a periodo fijo ----------
volatile uint32_t loop_count = 0;   // ciclos del lazo: si deja de subir, el núcleo 1 está trabado

void core1_main() {
    absolute_time_t next = get_absolute_time();
    while (true) {
        next = delayed_by_us(next, CONTROL_PERIOD_US);
        busy_wait_until(next);
        for (Axis& a : axes) a.update();
        loop_count = loop_count + 1;
    }
}

// ---------- Núcleo 0: todo lo que no es tiempo real ----------

// El STM32 de protección enciende los EN de los drivers. Hay que atenderlo seguido (cada 20 ms),
// también mientras se espera el Enter y durante el home: si deja de recibir el latido, apaga los motores.
Stm32Link link;

void service() {
    link.set_loop_count(loop_count);
    link.service(axes, N_AXES);
}

// Espera ms milisegundos atendiendo al STM32
void wait_ms(uint32_t ms) {
    const absolute_time_t until = make_timeout_time_ms(ms);
    while (!time_reached(until)) {
        service();
        sleep_ms(2);
    }
}

// Si pasa este tiempo sin un comando válido, cada eje se queda quieto donde está (hombre muerto).
// El PC debe repetir el objetivo más seguido que esto mientras quiera mover el robot.
constexpr uint32_t WATCHDOG_MS = 200;

// Arma una línea sin bloquear: devuelve true cuando llegó el Enter y la línea está en buf
bool poll_line(char* buf, int size, int& n) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\n' || c == '\r') {
            if (n == 0) continue;
            buf[n] = '\0';
            n = 0;
            return true;
        }
        if (n < size - 1) buf[n++] = static_cast<char>(c);
    }
    return false;
}

// Lee "r1,r2,p" o "r1,r2,p,servo,bomba" (servo en grados 0..180, bomba de 0 a 1).
// Devuelve cuántos números leyó (3 o 5), o 0 si el formato no es válido
int parse_targets(const char* line, float t[5]) {
    const char* p = line;
    for (int i = 0; i < 5; ++i) {
        char* end;
        t[i] = strtof(p, &end);
        if (end == p) return 0;
        if (*end == '\0') return (i == 2 || i == 4) ? i + 1 : 0;
        if (*end != ',') return 0;
        p = end + 1;
    }
    return 0;
}

char state_char(AxisState s) {
    switch (s) {
        case AxisState::Ready:  return 'R';
        case AxisState::Homing: return 'H';
        case AxisState::Fault:  return 'F';
        default:                return 'D';
    }
}

// Home de un eje, imprimiendo el avance. Devuelve true si terminó en Ready.
bool home(int i) {
    Axis& a = axes[i];
    link.set_cycle(CYCLE_HOME_R1 + i);
    a.start_homing();
    while (a.state() == AxisState::Homing) {
        printf("%s %-6s %8.4f rad | limite=%d | %4d mA | ciclos=%lu\n", a.name(),
               a.homing_phase() == HomingPhase::Search ? "Search" : "GoTo",
               a.position_rad(), a.limit(), link.current_mA(i), (unsigned long)loop_count);
        wait_ms(100);
    }
    if (a.state() != AxisState::Ready) {
        if (link.fault_mask() & (1u << i))
            printf("%s: FALLA, el STM32 corto el motor: %s\n", a.name(), Stm32Link::cause_name(link.cause(i)));
        else if (a.end_limit())
            printf("%s: FALLA, toco el final de carrera del otro extremo\n", a.name());
        else
            printf("%s: FALLA, el home tardo demasiado%s\n", a.name(),
                   link.ok() ? "" : " (el STM32 no responde: sin el no se encienden los drivers)");
        return false;
    }
    printf("%s: HOME OK. Posicion = %.4f rad\n", a.name(), a.position_rad());
    return true;
}

int main() {
    stdio_init_all();
    for (Axis& a : axes) a.init();      // hardware listo antes de arrancar el lazo
    actuators::init();
    link.init();
    multicore_launch_core1(core1_main);

    wait_ms(3000);                      // tiempo para abrir el monitor serie
    printf("\n=== Home de %d ejes, uno por uno ===\n", N_AXES);
    printf("STM32 de proteccion: %s\n", link.present() ? "OK" : "NO RESPONDE (los drivers no se van a encender)");
    printf("Presiona ENTER para iniciar. Mantén la mano cerca del E-stop.\n");
    link.set_cycle(CYCLE_WAIT);
    int c;
    do {
        service();
        c = getchar_timeout_us(2000);
    } while (c != '\n' && c != '\r');

    for (int i = 0; i < N_AXES; ++i) {
        if (!home(i)) {                 // si un eje falla, no se sigue
            link.set_cycle(CYCLE_FAULT);
            while (true) wait_ms(1000);
        }
    }

    // Control: el PC manda "r1,r2,p" (rad, rad, mm) y Enter, varias veces por segundo.
    // Opcional: "r1,r2,p,servo,bomba" para mover también el servo (grados) y la bomba (0..1).
    // A cada línea se responde "pos_r1,pos_r2,pos_p,estados" (p. ej. "0.5012,-0.2000,35.120,RRR"), o "ERR".
    printf("LISTO\n");
    link.set_cycle(CYCLE_READY);
    bool fault_reported = false;
    char line[64];
    int n = 0;
    absolute_time_t last_cmd = get_absolute_time();
    bool holding = false;   // true cuando el watchdog ya dejó los ejes quietos

    while (true) {
        service();
        if (poll_line(line, sizeof(line), n)) {
            float t[5];
            const int k = parse_targets(line, t);
            if (k > 0) {
                for (int i = 0; i < N_AXES; ++i) axes[i].set_target_rad(t[i]);
                if (k == 5) {
                    actuators::servo_deg(t[3]);
                    actuators::pump(t[4]);
                }
                last_cmd = get_absolute_time();
                holding = false;
                printf("%.4f,%.4f,%.3f,%c%c%c\n",
                       axes[0].position_rad(), axes[1].position_rad(), axes[2].position_rad(),
                       state_char(axes[0].state()), state_char(axes[1].state()), state_char(axes[2].state()));
            } else {
                printf("ERR\n");
            }
        }

        // Watchdog: el PC dejó de mandar (soltó el botón, se colgó o se desconectó) -> quedarse quieto
        if (!holding && absolute_time_diff_us(last_cmd, get_absolute_time()) > WATCHDOG_MS * 1000) {
            for (Axis& a : axes) a.set_target_rad(a.position_rad());
            holding = true;
        }

        // Ciclo para la telemetría, y aviso (una vez) si el STM32 cortó algún eje
        bool any_fault = false;
        for (Axis& a : axes) any_fault |= a.state() == AxisState::Fault;
        link.set_cycle(any_fault ? CYCLE_FAULT : holding ? CYCLE_HOLD : CYCLE_READY);
        if (any_fault && !fault_reported) {
            for (int i = 0; i < N_AXES; ++i)
                if (link.fault_mask() & (1u << i))
                    printf("# FALLA %s: %s\n", axes[i].name(), Stm32Link::cause_name(link.cause(i)));
            fault_reported = true;
        }
    }
}
