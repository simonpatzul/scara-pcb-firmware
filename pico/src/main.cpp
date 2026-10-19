#include <cstdio>
#include <cstdlib>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "axis.h"

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

// Lee "r1,r2,p". Devuelve false si el formato no es válido
bool parse_targets(const char* line, float t[3]) {
    char* end;
    t[0] = strtof(line, &end);    if (end == line || *end != ',') return false;
    t[1] = strtof(end + 1, &end); if (*end != ',') return false;
    t[2] = strtof(end + 1, &end); return *end == '\0';
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
bool home(Axis& a) {
    a.start_homing();
    while (a.state() == AxisState::Homing) {
        printf("%s %-6s %8.4f rad | limite=%d | ciclos=%lu\n", a.name(),
               a.homing_phase() == HomingPhase::Search ? "Search" : "GoTo",
               a.position_rad(), a.limit(), (unsigned long)loop_count);
        sleep_ms(100);
    }
    if (a.state() != AxisState::Ready) {
        printf("%s: FALLA, el home tardo demasiado\n", a.name());
        return false;
    }
    printf("%s: HOME OK. Posicion = %.4f rad\n", a.name(), a.position_rad());
    return true;
}

int main() {
    stdio_init_all();
    for (Axis& a : axes) a.init();      // hardware listo antes de arrancar el lazo
    multicore_launch_core1(core1_main);

    sleep_ms(3000);                     // tiempo para abrir el monitor serie
    printf("\n=== Home de %d ejes, uno por uno ===\n", N_AXES);
    printf("Presiona ENTER para iniciar. Mantén la mano cerca del E-stop.\n");
    int c;
    do { c = getchar(); } while (c != '\n' && c != '\r');

    for (Axis& a : axes) {
        if (!home(a)) while (true) sleep_ms(1000);   // si un eje falla, no se sigue
    }

    // Control: el PC manda "r1,r2,p" (rad, rad, mm) y Enter, varias veces por segundo.
    // A cada línea se responde "pos_r1,pos_r2,pos_p,estados" (p. ej. "0.5012,-0.2000,35.120,RRR"), o "ERR".
    printf("LISTO\n");
    char line[48];
    int n = 0;
    absolute_time_t last_cmd = get_absolute_time();
    bool holding = false;   // true cuando el watchdog ya dejó los ejes quietos

    while (true) {
        if (poll_line(line, sizeof(line), n)) {
            float t[3];
            if (parse_targets(line, t)) {
                for (int i = 0; i < N_AXES; ++i) axes[i].set_target_rad(t[i]);
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
    }
}
