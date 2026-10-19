# Arranca el bridge (puerto serie de la Pico) y la secuencia de la Estación 1.
#   ros2 launch scara_pc estacion1.launch.py port:=/dev/ttyACM0
# La secuencia pide Enter en la terminal, así que se lanza con su propia consola (prefix).
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    params = os.path.join(get_package_share_directory('scara_pc'), 'config', 'estacion1.yaml')
    return LaunchDescription([
        DeclareLaunchArgument('port', default_value='/dev/ttyACM0'),
        Node(package='scara_pc', executable='bridge', parameters=[{'port': LaunchConfiguration('port')}]),
        Node(package='scara_pc', executable='estacion1', parameters=[params],
             prefix='xterm -e', output='screen'),
    ])
