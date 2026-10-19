from setuptools import setup

setup(
    name='scara_pc',
    version='0.1.0',
    packages=['scara_pc'],
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/scara_pc']),
        ('share/scara_pc', ['package.xml']),
    ],
    install_requires=['setuptools'],
    entry_points={
        'console_scripts': [
            # nombre_del_ejecutable = archivo:funcion   (cada nodo nuevo se agrega aquí)
            'bridge = scara_pc.bridge:main',
            'teleop = scara_pc.teleop:main',
            'p2p = scara_pc.p2p:main',
        ],
    },
)
