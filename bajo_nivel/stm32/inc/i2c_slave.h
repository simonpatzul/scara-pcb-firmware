#pragma once
// I2C1 como esclavo de la Pico en PB6 (SCL) / PB7 (SDA). Sirve el mapa de registros de regs.h.
void i2c_slave_init(void);
