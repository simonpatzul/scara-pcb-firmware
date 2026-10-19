# Puente PC <-> Pico. Es el ÚNICO nodo que toca el puerto serie.
#   Escucha  /scara/target        (Float64MultiArray: [r1 rad, r2 rad, p mm])
#   Publica  /scara/joint_states  (JointState: posiciones medidas, mismas unidades)
# Manda el último objetivo a la Pico a tasa fija, así su watchdog nunca se dispara
# mientras este nodo esté vivo. Si el nodo muere o se desconecta el USB, la Pico se queda quieta.
import serial
import rclpy
from rclpy.node import Node
from std_msgs.msg import Float64MultiArray
from sensor_msgs.msg import JointState

JOINTS = ['joint_r1', 'joint_r2', 'joint_p']


class Bridge(Node):
    def __init__(self):
        super().__init__('bridge')
        port = self.declare_parameter('port', '/dev/ttyACM0').value
        rate = self.declare_parameter('rate_hz', 20.0).value

        self.ser = serial.Serial(port, 115200, timeout=1.0)
        self.home()

        self.target = [0.0, 0.0, 0.0]   # después del home los tres ejes quedan en 0
        self.pub = self.create_publisher(JointState, '/scara/joint_states', 10)
        self.create_subscription(Float64MultiArray, '/scara/target', self.on_target, 10)
        self.create_timer(1.0 / rate, self.tick)

    def home(self):
        # La Pico espera un Enter para hacer el home y al terminar imprime LISTO
        self.get_logger().info('Haciendo home...')
        self.ser.write(b'\n')
        while True:
            line = self.ser.readline().decode(errors='ignore').strip()
            if line:
                self.get_logger().info(f'Pico: {line}')
            if line == 'LISTO':
                break
        self.ser.timeout = 0   # de aquí en adelante las lecturas no bloquean

    def on_target(self, msg):
        if len(msg.data) == 3:
            self.target = list(msg.data)

    def tick(self):
        r1, r2, p = self.target
        self.ser.write(f'{r1:.4f},{r2:.4f},{p:.3f}\n'.encode())

        # Leer todas las respuestas que hayan llegado y quedarse con la última válida
        last = None
        while self.ser.in_waiting:
            line = self.ser.readline().decode(errors='ignore').strip()
            parts = line.split(',')
            if len(parts) == 4:
                last = parts
        if last is None:
            return

        msg = JointState()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.name = JOINTS
        msg.position = [float(v) for v in last[:3]]
        self.pub.publish(msg)
        if 'F' in last[3]:
            self.get_logger().error(f'La Pico reporta falla: {last[3]}')


def main():
    rclpy.init()
    rclpy.spin(Bridge())


if __name__ == '__main__':
    main()
