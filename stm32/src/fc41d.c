#include "fc41d.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "shared.h"

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#warning "Falta stm32/inc/wifi_config.h: se usa wifi_config.example.h (copiala y pon tu red)"
#include "wifi_config.example.h"
#endif

volatile bool wifi_ok = false, mqtt_ok = false;

// ---------- UART ----------
#define RX_SIZE 256
static volatile uint8_t rx_buf[RX_SIZE];
static volatile uint16_t rx_head = 0, rx_tail = 0;

void USART1_IRQHandler(void) {
    if (USART1->ISR & USART_ISR_RXNE) {
        const uint8_t c = (uint8_t)USART1->RDR;
        const uint16_t next = (rx_head + 1) % RX_SIZE;
        if (next != rx_tail) { rx_buf[rx_head] = c; rx_head = next; }
    }
    if (USART1->ISR & USART_ISR_ORE) USART1->ICR = USART_ICR_ORECF;
}

static void uart_write(const char* s, int n) {
    for (int i = 0; i < n; ++i) {
        while (!(USART1->ISR & USART_ISR_TXE)) {}
        USART1->TDR = (uint8_t)s[i];
        if ((i & 63) == 63) watchdog_kick();   // un mensaje largo tarda ~40 ms a 115200
    }
}
static void uart_puts(const char* s) { uart_write(s, (int)strlen(s)); }

// ---------- Lectura de líneas ----------
static char line[128];
static int line_len = 0;
static bool prompt = false;   // el módulo mandó '>' (listo para recibir el mensaje de QMTPUB)

// Devuelve true cuando hay una línea completa en `line`
static bool read_line(void) {
    while (rx_tail != rx_head) {
        const char c = (char)rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) % RX_SIZE;
        if (c == '>' && line_len == 0) { prompt = true; continue; }
        if (c == '\r') continue;
        if (c == '\n') {
            if (line_len == 0) continue;
            line[line_len] = '\0';
            line_len = 0;
            return true;
        }
        if (line_len < (int)sizeof(line) - 1) line[line_len++] = c;
    }
    return false;
}

static bool starts(const char* prefix) { return strncmp(line, prefix, strlen(prefix)) == 0; }

// ---------- Máquina de estados ----------
typedef enum {
    S_POWER_OFF, S_BOOT, S_ECHO_OFF, S_WIFI, S_WIFI_WAIT_IP, S_MQTT_CFG, S_MQTT_OPEN, S_MQTT_CONN,
    S_READY, S_PUB_PROMPT, S_PUB_DONE, S_RETRY_WAIT
} State;

static State st = S_POWER_OFF;
static uint32_t t0 = 0;           // cuándo empezó el estado actual
static uint8_t fails = 0;
static char cmd[160];

static char pub_topic[48];
static char pub_payload[640];
static int pub_qos = 0;
static uint16_t pub_id = 1;

static void go(State s) { st = s; t0 = millis(); }
static uint32_t elapsed(void) { return millis() - t0; }

static void retry(void) {
    mqtt_ok = false;
    if (++fails >= 5) { fails = 0; wifi_ok = false; go(S_POWER_OFF); }   // reinicio completo del módulo
    else go(S_RETRY_WAIT);
}

void fc41d_init(void) {
    fc41d_power(false);
    go(S_POWER_OFF);
}

bool fc41d_ready(void) { return st == S_READY; }

bool fc41d_publish(const char* topic, const char* payload, int qos) {
    if (st != S_READY) return false;
    const int n = (int)strlen(payload);
    if (n >= (int)sizeof(pub_payload) || strlen(topic) >= sizeof(pub_topic)) return false;
    strcpy(pub_topic, topic);
    memcpy(pub_payload, payload, (size_t)n + 1);
    pub_qos = qos;
    pub_id = pub_id % 65535 + 1;
    prompt = false;
    // QMTPUBRAW: el módulo responde '>' y después recibe el mensaje tal cual (el JSON lleva comillas,
    // que en AT+QMTPUB irían dentro de otra cadena). QoS 0 exige msgID = 0
    snprintf(cmd, sizeof(cmd), "AT+QMTPUBRAW=0,%u,%d,0,\"%s\",%d\r\n", qos ? pub_id : 0, qos, pub_topic, n);
    uart_puts(cmd);
    go(S_PUB_PROMPT);
    return true;
}

void fc41d_poll(void) {
    const bool got = read_line();

    // Avisos que pueden llegar en cualquier momento
    if (got && starts("+QMTSTAT:")) { retry(); return; }   // el broker cerró la conexión

    switch (st) {
    case S_POWER_OFF:   // CEN es un reset activo en bajo: hay que tenerlo abajo al menos 100 ms
        fc41d_power(false);
        if (elapsed() > 200) { fc41d_power(true); go(S_BOOT); }
        break;

    case S_BOOT:   // el módulo tarda en arrancar
        if (elapsed() > 2000) { uart_puts("ATE0\r\n"); go(S_ECHO_OFF); }
        break;

    case S_ECHO_OFF:
        if (got && starts("OK")) {
            // Según el manual del FC41D, SSID y clave van sin comillas
            snprintf(cmd, sizeof(cmd), "AT+QSTAAPINFO=%s,%s\r\n", WIFI_SSID, WIFI_PASS);
            uart_puts(cmd);
            go(S_WIFI);
        } else if (elapsed() > 1000) retry();
        break;

    case S_WIFI:
        if (got && starts("OK")) go(S_WIFI_WAIT_IP);
        else if ((got && starts("ERROR")) || elapsed() > 15000) retry();
        break;

    case S_WIFI_WAIT_IP:   // espera +QSTASTAT:"GOT_IP"; si no llega con ese texto, igual intenta el broker
        if ((got && strstr(line, "GOT_IP")) || elapsed() > 8000) {
            wifi_ok = true;
            uart_puts("AT+QMTCFG=\"version\",0,4\r\n");   // MQTT 3.1.1
            go(S_MQTT_CFG);
        }
        break;

    case S_MQTT_CFG:
        if ((got && (starts("OK") || starts("ERROR"))) || elapsed() > 1000) {
            snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=0,\"%s\",%d\r\n", MQTT_HOST, MQTT_PORT);
            uart_puts(cmd);
            go(S_MQTT_OPEN);
        }
        break;

    case S_MQTT_OPEN:
        if (got && starts("+QMTOPEN: 0,0")) {
            if (MQTT_USER[0])
                snprintf(cmd, sizeof(cmd), "AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"\r\n", MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
            else
                snprintf(cmd, sizeof(cmd), "AT+QMTCONN=0,\"%s\"\r\n", MQTT_CLIENT_ID);
            uart_puts(cmd);
            go(S_MQTT_CONN);
        } else if ((got && (starts("+QMTOPEN:") || starts("ERROR"))) || elapsed() > 15000) {
            wifi_ok = false;
            retry();
        }
        break;

    case S_MQTT_CONN:
        if (got && starts("+QMTCONN: 0,0")) { mqtt_ok = true; fails = 0; go(S_READY); }
        else if ((got && (starts("+QMTCONN:") || starts("ERROR"))) || elapsed() > 15000) retry();
        break;

    case S_READY:
        break;

    case S_PUB_PROMPT:
        if (prompt) {
            prompt = false;
            uart_puts(pub_payload);
            go(S_PUB_DONE);
        } else if ((got && starts("ERROR")) || elapsed() > 3000) retry();
        break;

    case S_PUB_DONE:
        if (got && (starts("+QMTPUB: 0,") || starts("+QMTPUBRAW: 0,"))) {
            // +QMTPUB: 0,<msgID>,<result>  result 0 = enviado
            const char* last = strrchr(line, ',');
            if (last && last[1] == '0') { fails = 0; go(S_READY); }
            else retry();
        } else if (got && starts("ERROR")) retry();
        else if (elapsed() > 5000) retry();
        break;

    case S_RETRY_WAIT:
        if (elapsed() > 3000) {
            // Reintenta desde el broker si había Wi-Fi; si no, desde el Wi-Fi
            if (wifi_ok) {
                snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=0,\"%s\",%d\r\n", MQTT_HOST, MQTT_PORT);
                uart_puts(cmd);
                go(S_MQTT_OPEN);
            } else {
                uart_puts("ATE0\r\n");
                go(S_ECHO_OFF);
            }
        }
        break;
    }
}
