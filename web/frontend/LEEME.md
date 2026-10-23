# Página SCADA · Estación 1 (Pick & Place de PCB)

`index.html` es la HMI de la estación. Lo normal es abrirla desde el backend (ver `../README.md`): http://localhost:8000. También se puede abrir con doble clic en el navegador (necesita internet para la librería MQTT y las fuentes).

1. Abre `index.html`. Arranca en **modo demo**: simula el ciclo completo (pila en cruz → 4 tarjetas corregidas en la bandeja, unos 34 s). Diseño monocromático (blanco y negro, como la referencia de Simon), con tema claro u oscuro y tres pestañas: Resumen, Estación y Ejes. En computador cada pestaña cabe en una sola pantalla sin scroll (pensada para 1920×1080; se escala a otras resoluciones); en celular las tarjetas se apilan.
2. Pulsa **Conexión**, revisa el broker (`wss://broker.hivemq.com:8884/mqtt`) y pon el mismo prefijo que tiene el STM32 en `stm32/inc/wifi_config.h` (por defecto `scara`).
3. Pulsa **Conectar**. La página lee `<prefijo>/estado` (cada 500 ms) y `<prefijo>/evento` (fallas), formato v1 del firmware.

Es solo de lectura: no manda comandos al robot. Los rangos de los ejes, el patrón de la pila (`PILA: [0, 90, 0, 90]`) y la ventana de 20–50 s están al inicio del script, en `CFG`.

## Campo `est1` (panel de la estación)

Lo publica el PC (ROS 2) en su propio topic `<prefijo>/est1`, con retain. La página también lo acepta dentro de `scara/estado`. Sin él, el resto de la página funciona igual.

```json
{ "tarjeta": 2, "fase": "BANDEJA", "vacio": 1, "colocadas": 1, "t_ciclo": 12.8, "t_ultimo": 34.5, "ciclos": 3 }
```

| Campo | Significado |
|---|---|
| `tarjeta` | tarjeta en curso, 1 a 4 (1 = la de arriba de la pila); `null` entre ciclos |
| `fase` | `PILA`, `BAJAR_PILA`, `VACIO`, `SUBIR_PILA`, `CORREGIR`, `BANDEJA`, `BAJAR_BANDEJA`, `SOLTAR`, `SUBIR_BANDEJA`, o `ESPERA_PILA` entre ciclos |
| `vacio` | ventosa (bomba) 0/1 |
| `colocadas` | tarjetas ya dejadas en la bandeja en este ciclo, 0 a 4 |
| `t_ciclo` | segundos desde que empezó el ciclo actual |
| `t_ultimo` | duración del último ciclo terminado, s (0 si no hay) |
| `ciclos` | ciclos terminados desde el arranque |
