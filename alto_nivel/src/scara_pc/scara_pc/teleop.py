# Movimiento manual con teclado (solo terminal) + hombre muerto.
#   Escucha  /scara/joint_states  (posición medida)
#   Publica  /scara/target        ([r1 rad, r2 rad, p mm])
#
# MANTÉN presionada la tecla para mover; al soltarla el robot se queda quieto (hombre muerto).
#   Q / A = R1 + / -     W / S = R2 + / -     E / D = P + / -
#
# La terminal no avisa cuando se suelta una tecla, pero mientras está presionada la REPITE:
# primero llega una vez, luego hay una pausa (~0.5 s, retardo del teclado) y después llega
# ~30 veces por segundo. Si deja de llegar, se toma como soltada.
import sys
import time
import select
import termios
import tty
import rclpy
from rclpy.node import Node
from std_msgs.msg import Float64MultiArray
from sensor_msgs.msg import JointState

# Cuánto adelante de la posición medida se pone el objetivo mientras la tecla está presionada.
# Más grande = jog más rápido. Unidades: rad, rad, mm.
LEAD = [0.7, 0.4, 5.0]
FIRST_S = 0.8    # espera tras la PRIMERA pulsación: mayor que el retardo del teclado
REPEAT_S = 0.05  # espera cuando ya está repitiendo: si no llega en este tiempo, se soltó
STOP_S = 0.15     # al soltar, el objetivo sigue al robot este tiempo mientras frena y luego se fija
KEYS = {'q': (0, +1), 'a': (0, -1), 'w': (1, +1), 's': (1, -1), 'e': (2, +1), 'd': (2, -1)}


class Teleop(Node):
    def __init__(self):
        super().__init__('teleop')
        self.meas = None        # última posición medida
        self.target = None      # objetivo que se está mandando
        self.key = None         # tecla que se está sosteniendo (None = ninguna)
        self.last_key_time = 0.0
        self.repeating = False  # ¿ya empezó la repetición automática del teclado?
        self.stop_until = None  # fin de la fase de frenado (None = no está frenando)
        self.pub = self.create_publisher(Float64MultiArray, '/scara/target', 10)
        self.create_subscription(JointState, '/scara/joint_states', self.on_state, 10)
        self.create_timer(0.05, self.tick)   # 20 Hz
        self.get_logger().info('Mantén Q/A (R1), W/S (R2), E/D (P). Ctrl+C para salir.')

    def on_state(self, msg):
        self.meas = list(msg.position)

    def publish(self):
        self.pub.publish(Float64MultiArray(data=self.target))

    def tick(self):
        # Leer todas las teclas que llegaron desde el último ciclo, sin bloquear
        while select.select([sys.stdin], [], [], 0)[0]:
            c = sys.stdin.read(1).lower()
            if c in KEYS:
                if c != self.key and self.meas is not None:
                    self.target = list(self.meas)   # tecla nueva: arranca desde donde está
                    self.repeating = False
                else:
                    self.repeating = True           # misma tecla otra vez: ya está repitiendo
                self.key = c
                self.last_key_time = time.monotonic()
                self.stop_until = None

        if self.meas is None:
            return
        now = time.monotonic()

        # Frenando: el objetivo sigue al robot mientras pierde la inercia, así no se devuelve
        if self.stop_until is not None:
            self.target = list(self.meas)
            self.publish()
            if now > self.stop_until:
                self.stop_until = None
            return

        if self.key is None:
            return

        wait = REPEAT_S if self.repeating else FIRST_S
        if now - self.last_key_time > wait:   # se soltó: empezar a frenar
            self.key = None
            self.stop_until = now + STOP_S
            return

        axis, d = KEYS[self.key]
        self.target[axis] = self.meas[axis] + d * LEAD[axis]
        self.publish()


def main():
    rclpy.init()
    old = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin)            # entrega cada tecla al instante y sin eco; Ctrl+C sigue funcionando
    try:
        rclpy.spin(Teleop())
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)   # deja la terminal como estaba


if __name__ == '__main__':
    main()
