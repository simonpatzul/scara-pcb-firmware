#pragma once
// I2C1 como esclavo de la Pico en PB6 (SCL) / PB7 (SDA). Sirve el mapa de registros de regs.h.
void i2c_slave_init(void);

// Llamar desde el bucle principal: si el bus queda ocupado más de I2C_STUCK_MS (una transacción normal dura
// menos de 3 ms), reinicia el periférico para que un corte a mitad de mensaje no deje el I2C trabado.
void i2c_slave_poll(void);
