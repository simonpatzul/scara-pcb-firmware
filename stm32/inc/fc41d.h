#pragma once
// Módulo Wi-Fi Quectel FC41D por USART1 con comandos AT: conexión al Wi-Fi y publicación MQTT.
// Todo es no bloqueante: fc41d_poll() avanza una máquina de estados desde el bucle principal.
//
// Los comandos (AT+QSTAAPINFO, AT+QMTCFG, AT+QMTOPEN, AT+QMTCONN, AT+QMTPUBRAW) siguen el manual AT V1.0
// del FC41D, pero no se probaron con un módulo real. Si algo no responde como se espera, mirar las
// respuestas en el UART del módulo y ajustar las cadenas en fc41d.c.
#include <stdbool.h>

void fc41d_init(void);
void fc41d_poll(void);
bool fc41d_ready(void);    // conectado al broker y sin publicación en curso
bool fc41d_publish(const char* topic, const char* payload, int qos);   // false si está ocupado o desconectado
