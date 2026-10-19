# Punto a punto: consigna (x, y) + p -> IK -> ángulos -> /scara/target.
# Se escribe "x y p" en la terminal (mm). El bridge sigue reenviando el último objetivo a la Pico.
#   Publica  /scara/target  ([q1 rad, q2 rad, p mm])
import rclpy                                   # la librería de ROS 2 para Python
from rclpy.node import Node                    # la clase base de todo nodo
from std_msgs.msg import Float64MultiArray     # el tipo de mensaje que espera el bridge
from scara_pc.ik import ik                     # tu cinemática inversa


class P2P(Node):                               # nuestro nodo ES un Node (hereda de él)
    def __init__(self):
        super().__init__('p2p')                # nombre del nodo (sale en `ros2 node list`)
        # publisher: (tipo de mensaje, nombre del topic, tamaño de la cola)
        self.pub = self.create_publisher(Float64MultiArray, '/scara/target', 10)

    def send(self, q1, q2, p):
        self.pub.publish(Float64MultiArray(data=[q1, q2, p]))
        self.get_logger().info(f'Objetivo enviado: q1={q1:.4f} rad, q2={q2:.4f} rad, p={p:.2f} mm')


def main():
    rclpy.init()                               # arranca la comunicación de ROS
    node = P2P()

    # Esperar a que el bridge esté escuchando; si no, el primer mensaje se pierde
    while node.pub.get_subscription_count() == 0:
        rclpy.spin_once(node, timeout_sec=0.1)

    print('Escribe "x y p" en mm (Ctrl+C para salir)')
    try:
        while True:
            line = input('> ')
            try:
                x, y, p = (float(v) for v in line.split())   # "100 150 0" -> 100.0, 150.0, 0.0
            except ValueError:
                print('  Formato: x y p')
                continue

            q = ik(x, y, p)
            if q is None:
                print('  Fuera de alcance')
                continue
            node.send(q[1], -q[0], q[2])
    except (KeyboardInterrupt, EOFError):
        pass

    node.destroy_node()
    rclpy.try_shutdown()                       # cierra ROS (sin error si Ctrl+C ya lo cerró)

