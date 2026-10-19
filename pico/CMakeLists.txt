cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico)
include(pico_sdk_import.cmake)

project(scara_fw C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(scara_fw
    src/main.cpp
    src/encoder.cpp
    src/axis.cpp
    src/motor_driver.cpp
    src/limit_switch.cpp
    src/rst_controller.cpp
)
pico_generate_pio_header(scara_fw ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(scara_fw PRIVATE config)
target_link_libraries(scara_fw pico_stdlib pico_multicore hardware_pio hardware_pwm)

pico_enable_stdio_usb(scara_fw 1)
pico_enable_stdio_uart(scara_fw 0)
pico_add_extra_outputs(scara_fw)
