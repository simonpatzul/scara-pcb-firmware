cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico)
include(pico_sdk_import.cmake)

project(scara_fw C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# Firmware principal
add_executable(scara_fw
    src/main.cpp
    src/encoder.cpp
    src/axis.cpp
    src/motor_driver.cpp
    src/limit_switch.cpp
    src/rst_controller.cpp
    src/soft_i2c.cpp
    src/stm32_link.cpp
    src/actuators.cpp
)
pico_generate_pio_header(scara_fw ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(scara_fw PRIVATE config)
target_link_libraries(scara_fw pico_stdlib pico_multicore hardware_pio hardware_pwm hardware_adc)
pico_enable_stdio_usb(scara_fw 1)
pico_enable_stdio_uart(scara_fw 0)
pico_add_extra_outputs(scara_fw)

# Prueba de ejes en lazo abierto (sentido del motor, signo del encoder, finales de carrera)
add_executable(scara_prueba
    src/prueba_eje.cpp
    src/encoder.cpp
    src/motor_driver.cpp
    src/limit_switch.cpp
    src/soft_i2c.cpp
    src/stm32_link.cpp
)
pico_generate_pio_header(scara_prueba ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(scara_prueba PRIVATE config)
target_link_libraries(scara_prueba pico_stdlib hardware_pio hardware_pwm hardware_adc)
pico_enable_stdio_usb(scara_prueba 1)
pico_enable_stdio_uart(scara_prueba 0)
pico_add_extra_outputs(scara_prueba)
