# Estación 1: pick & place de 4 PCB.
# Toma las tarjetas de una pila (orientación alternada 0/90°) y las deja en la bandeja, todas con el
# agujero H1 abajo a la izquierda. Movimiento punto a punto; el ciclo de 4 tarjetas debe durar 20–50 s.
#   Publica  /scara/target        ([r1 rad, r2 rad, p mm, servo grados, bomba 0..1])
#   Escucha  /scara/joint_states  (posición medida, para saber cuándo llegó a cada punto)
#   MQTT     scara/est1           (estado de la secuencia para la página SCADA; opcional)
#
# Las posiciones están en config/estacion1.yaml. Mientras `datos_listos` sea false, el nodo no mueve el robot.
# Uso:  ros2 run scara_pc estacion1 --ros-args --params-file <ruta>/estacion1.yaml
#       Enter = empezar un ciclo (con una pila nueva puesta), Ctrl+C = salir
import json
import math
import threading
import time
import rclpy
from rclpy.node import Node
from std_msgs.msg import Float64MultiArray
from sensor_msgs.msg import JointState
from scara_pc.ik import ik

try:
    import paho.mqtt.client as mqtt
except ImportError:
    mqtt = None

T_MIN, T_MAX = 20.0, 50.0   # ventana de tiempo de ciclo de la estación


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


class Estacion1(Node):
    def __init__(self):
        super().__init__('estacion1')
        P = lambda name, default: self.declare_parameter(name, default).value
        self.datos_listos = P('datos_listos', False)
        self.pila_xy = (P('pila_x', 0.0), P('pila_y', 0.0))
        self.p_pila_primera = P('p_pila_primera', 0.0)       # p de la tarjeta de arriba (mm)
        self.grosor = P('grosor_tarjeta', 1.6)               # mm; cada tarjeta siguiente está más abajo
        self.orient_pila = list(P('orientaciones_pila', [0.0, 90.0, 0.0, 90.0]))   # grados, de arriba hacia abajo
        self.bandeja_x = list(P('bandeja_x', [0.0, 0.0, 0.0, 0.0]))
        self.bandeja_y = list(P('bandeja_y', [0.0, 0.0, 0.0, 0.0]))
        self.p_bandeja = P('p_bandeja', 0.0)
        self.orient_bandeja = P('orientacion_bandeja', 0.0)  # orientación con H1 abajo a la izquierda
        self.p_seguro = P('p_seguro', 0.0)                   # altura para moverse sin chocar
        self.servo_signo = P('servo_signo', 1.0)             # +1 si el servo gira en el mismo sentido que q1+q2
        self.t_vacio = P('t_vacio', 0.5)
        self.t_soltar = P('t_soltar', 0.5)
        self.t_servo = P('t_servo', 0.4)
        self.tol_rad = P('tolerancia_rad', 0.02)
        self.tol_mm = P('tolerancia_mm', 1.0)
        self.t_mov_max = P('t_movimiento_max', 10.0)
        mqtt_host = P('mqtt_host', 'broker.hivemq.com')
        mqtt_port = P('mqtt_port', 1883)

        self.pub = self.create_publisher(Float64MultiArray, '/scara/target', 10)
        self.create_subscription(JointState, '/scara/joint_states', self.on_state, 10)
        self.meas = None
        self.servo = 90.0
        self.bomba = 0.0

        # Estado para la SCADA (mismos nombres que el campo est1 de la página)
        self.est = {'tarjeta': None, 'fase': 'ESPERA_PILA', 'vacio': 0, 'colocadas': 0,
                    't_ciclo': 0.0, 't_ultimo': 0.0, 'ciclos': 0}
        self.t_inicio = None
        self.mqtt = None
        if mqtt is None:
            self.get_logger().warn('Sin paho-mqtt (pip install paho-mqtt): la SCADA no verá la secuencia')
        else:
            try:
                try:
                    self.mqtt = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)   # paho 2.x
                except AttributeError:
                    self.mqtt = mqtt.Client()                                    # paho 1.x
                self.mqtt.connect_async(mqtt_host, mqtt_port)
                self.mqtt.loop_start()
            except Exception as e:
                self.get_logger().warn(f'MQTT no disponible: {e}')
                self.mqtt = None
        self.create_timer(1.0, self.publish_est)

    # ---------- Comunicación ----------
    def on_state(self, msg):
        self.meas = list(msg.position)

    def publish_est(self):
        if self.t_inicio is not None:
            self.est['t_ciclo'] = round(time.monotonic() - self.t_inicio, 1)
        self.est['vacio'] = 1 if self.bomba > 0 else 0
        if self.mqtt is not None:
            self.mqtt.publish('scara/est1', json.dumps(self.est), qos=0, retain=True)

    def fase(self, nombre):
        self.est['fase'] = nombre
        self.get_logger().info(f'Tarjeta {self.est["tarjeta"]}: {nombre}')
        self.publish_est()

    def send(self, q):
        self.pub.publish(Float64MultiArray(data=[q[0], q[1], q[2], self.servo, self.bomba]))

    # ---------- Movimientos ----------
    def joints(self, x, y, p):
        """(x, y, p) en mm -> [r1, r2, p] con el mismo mapeo que p2p.py, y el ángulo del brazo (grados)."""
        q = ik(x, y, p)
        if q is None:
            raise ValueError(f'({x}, {y}) fuera de alcance')
        return [q[1], -q[0], q[2]], math.degrees(q[0] + q[1])

    def move(self, q):
        """Manda el objetivo y espera a que el robot llegue (o falla por tiempo)."""
        t0 = time.monotonic()
        while rclpy.ok():
            self.send(q)
            m = self.meas
            if m is not None and abs(m[0] - q[0]) < self.tol_rad and abs(m[1] - q[1]) < self.tol_rad \
                    and abs(m[2] - q[2]) < self.tol_mm:
                return
            if time.monotonic() - t0 > self.t_mov_max:
                raise RuntimeError(f'No llegó a {q} (¿falla de un eje?)')
            time.sleep(0.05)

    def hold(self, q, seconds):
        """Se queda en q mandando el objetivo (servo y bomba incluidos) durante `seconds`."""
        t0 = time.monotonic()
        while rclpy.ok() and time.monotonic() - t0 < seconds:
            self.send(q)
            time.sleep(0.05)

    # ---------- Secuencia ----------
    def check(self):
        """Verifica que todos los puntos estén al alcance antes de mover nada."""
        self.joints(*self.pila_xy, self.p_seguro)
        for x, y in zip(self.bandeja_x, self.bandeja_y):
            self.joints(x, y, self.p_seguro)
        if len(self.orient_pila) != 4 or len(self.bandeja_x) != 4 or len(self.bandeja_y) != 4:
            raise ValueError('orientaciones_pila, bandeja_x y bandeja_y deben tener 4 valores')

    def tarjeta(self, k):
        """Lleva la tarjeta k (0 = la de arriba) de la pila a su lugar en la bandeja."""
        self.est['tarjeta'] = k + 1
        q_pila_alto, brazo_pila = self.joints(*self.pila_xy, self.p_seguro)
        q_pila_bajo, _ = self.joints(*self.pila_xy, self.p_pila_primera + k * self.grosor)
        xb, yb = self.bandeja_x[k], self.bandeja_y[k]
        q_band_alto, brazo_band = self.joints(xb, yb, self.p_seguro)
        q_band_bajo, _ = self.joints(xb, yb, self.p_bandeja)

        # Giro que necesita la tarjeta: lo que le falta para quedar con H1 abajo a la izquierda, menos lo
        # que ya la gira el brazo al ir de la pila a la bandeja. Se reparte alrededor de 90° para que los
        # dos ángulos del servo queden dentro de 0..180.
        giro = wrap180((self.orient_bandeja - self.orient_pila[k]) - (brazo_band - brazo_pila))
        giro_servo = self.servo_signo * giro
        servo_pila, servo_band = 90.0 - giro_servo / 2, 90.0 + giro_servo / 2

        self.fase('PILA')
        self.servo, self.bomba = servo_pila, 0.0
        self.move(q_pila_alto)
        self.fase('BAJAR_PILA')
        self.move(q_pila_bajo)
        self.fase('VACIO')
        self.bomba = 1.0
        self.hold(q_pila_bajo, self.t_vacio)
        self.fase('SUBIR_PILA')
        self.move(q_pila_alto)
        self.fase('CORREGIR')
        self.servo = servo_band
        self.hold(q_pila_alto, self.t_servo)
        self.fase('BANDEJA')
        self.move(q_band_alto)
        self.fase('BAJAR_BANDEJA')
        self.move(q_band_bajo)
        self.fase('SOLTAR')
        self.bomba = 0.0
        self.hold(q_band_bajo, self.t_soltar)
        self.est['colocadas'] = k + 1
        self.fase('SUBIR_BANDEJA')
        self.move(q_band_alto)

    def ciclo(self):
        self.t_inicio = time.monotonic()
        self.est.update({'colocadas': 0, 't_ciclo': 0.0})
        for k in range(4):
            self.tarjeta(k)
        dur = time.monotonic() - self.t_inicio
        self.t_inicio = None
        self.est.update({'tarjeta': None, 't_ciclo': 0.0, 't_ultimo': round(dur, 1),
                         'ciclos': self.est['ciclos'] + 1})
        self.fase('ESPERA_PILA')
        msg = f'Ciclo terminado en {dur:.1f} s'
        if T_MIN <= dur <= T_MAX:
            self.get_logger().info(msg)
        else:
            self.get_logger().warn(f'{msg}: fuera de la ventana de {T_MIN:.0f}–{T_MAX:.0f} s')


def main():
    rclpy.init()
    node = Estacion1()
    threading.Thread(target=rclpy.spin, args=(node,), daemon=True).start()

    if not node.datos_listos:
        node.get_logger().error('Faltan las posiciones de la pila y la bandeja en estacion1.yaml '
                                '(poner datos_listos: true cuando estén medidas)')
        rclpy.try_shutdown()
        return
    try:
        node.check()
        while node.pub.get_subscription_count() == 0:   # esperar al bridge
            time.sleep(0.1)
        while rclpy.ok():
            input('Pon la pila y presiona Enter para empezar el ciclo (Ctrl+C para salir) ')
            try:
                node.ciclo()
            except RuntimeError as e:
                node.bomba = 0.0                     # soltar la bomba y quedarse donde está
                if node.meas is not None:
                    node.send(node.meas)
                node.get_logger().error(f'Ciclo detenido: {e}')
                node.fase('ESPERA_PILA')
    except (KeyboardInterrupt, EOFError):
        pass
    except ValueError as e:
        node.get_logger().error(str(e))
    node.destroy_node()
    rclpy.try_shutdown()


if __name__ == '__main__':
    main()
