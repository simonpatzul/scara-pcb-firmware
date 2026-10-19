#pragma once
// Copia este archivo como wifi_config.h y pon tus datos. wifi_config.h está en .gitignore
// para que la contraseña del Wi-Fi no termine en el repositorio.

#define WIFI_SSID       "mi_red"
#define WIFI_PASS       "mi_clave"

#define MQTT_HOST       "broker.hivemq.com"   // broker público, solo para pruebas
#define MQTT_PORT       1883
#define MQTT_CLIENT_ID  "scara-rrp-stm32"
#define MQTT_USER       ""                    // vacío = sin usuario
#define MQTT_PASS       ""
#define MQTT_PREFIX     "scara/"              // los topics quedan scara/estado y scara/evento
