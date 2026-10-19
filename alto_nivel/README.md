# Alto nivel · PC con ROS 2

Workspace de ROS 2 que corre en el PC y habla con la Pico por USB. Parte de [jer0valencia/SCARA_RRP](https://github.com/jer0valencia/SCARA_RRP) (commit 26dd87b).

| Paquete / nodo | Qué hace |
|---|---|
| `scara_description` | URDF del robot y `display.launch.py` para verlo en RViz |
| `scara_pc/bridge` | Único nodo que usa el puerto serie: hace el home y manda el objetivo a la Pico a 20 Hz |
| `scara_pc/teleop` | Mover el robot con el teclado |
| `scara_pc/p2p` | Mandar un punto `x y p` desde la terminal |
| `scara_pc/estacion1` | **Nuevo.** Pick & place de la Estación 1 |

Cambio en `bridge.py`: además de `[r1, r2, p]`, acepta `[r1, r2, p, servo, bomba]` en `/scara/target` y se lo pasa a la Pico como `r1,r2,p,servo,bomba`. Con 3 valores funciona como antes.

## Compilar

```
cd alto_nivel
colcon build
source install/setup.bash
pip install paho-mqtt     # opcional: para que la SCADA vea la secuencia
```

## Estación 1 (pick & place)

Toma las 4 tarjetas de la pila (orientación alternada 0/90°) y las deja en la bandeja con H1 abajo a la izquierda. Para cada tarjeta: va sobre la pila, baja, prende la bomba, sube, gira el servo para corregir la orientación, va a su lugar en la bandeja, baja, suelta y sube. El giro del servo descuenta lo que gira el brazo entre la pila y la bandeja. Al terminar avisa si el ciclo quedó fuera de 20–50 s.

1. **Medir las posiciones** y ponerlas en `src/scara_pc/config/estacion1.yaml`: centro de la pila, `p` de la tarjeta de arriba, grosor de la tarjeta, los 4 lugares de la bandeja, la altura segura y la orientación que deja H1 abajo a la izquierda. Después cambiar `datos_listos` a `true`. Mientras siga en `false`, el nodo no mueve el robot.
2. Volver a compilar (`colcon build`) para que se instale el yaml.
3. Correr:
   ```
   ros2 launch scara_pc estacion1.launch.py port:=/dev/ttyACM0
   ```
   o en dos terminales: `ros2 run scara_pc bridge` y `ros2 run scara_pc estacion1 --ros-args --params-file src/scara_pc/config/estacion1.yaml`.
4. Poner la pila y presionar Enter para cada ciclo.

Si un movimiento no llega en `t_movimiento_max` (por ejemplo, porque un eje quedó en falla), el ciclo se detiene, se apaga la bomba y el robot se queda donde está.

Hay que comprobar con el robot: que `p` crezca hacia abajo (el yaml lo supone así) y el sentido del servo (`servo_signo`).
