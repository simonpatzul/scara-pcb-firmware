cmake_minimum_required(VERSION 3.8)
project(scara_description)
find_package(ament_cmake REQUIRED)

# Este paquete no compila nada: solo copia sus carpetas a install/ para que ROS las encuentre
install(DIRECTORY urdf launch DESTINATION share/${PROJECT_NAME})

ament_package()
