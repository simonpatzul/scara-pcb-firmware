# Backend de la página SCADA.
# Se suscribe al broker MQTT (lo que publican el STM32 y el PC), guarda el historial en SQLite y lo entrega
# a la página por HTTP y WebSocket. También sirve el frontend (../frontend).
#
#   GET /api/estado           último scara/estado y scara/est1, con su antigüedad en segundos
#   GET /api/eventos?n=50     últimas fallas (scara/evento)
#   GET /api/ciclos?n=50      últimos ciclos de la Estación 1 y su resumen (promedio, dentro de 20–50 s)
#   GET /api/historial?min=10 muestras cada 2 s: corriente, voltaje y posición de cada eje y temperaturas
#   WS  /ws                   en vivo: {"topic": "estado" | "evento" | "est1", "data": {...}}
#
# Configuración por variables de entorno: MQTT_HOST, MQTT_PORT, MQTT_PREFIX, DB_PATH, MUESTRA_S.
import asyncio
import contextlib
import json
import os
import sqlite3
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles

MQTT_HOST = os.environ.get('MQTT_HOST', 'broker.hivemq.com')
MQTT_PORT = int(os.environ.get('MQTT_PORT', '1883'))
PREFIX = os.environ.get('MQTT_PREFIX', 'scara')
DB_PATH = os.environ.get('DB_PATH', str(Path(__file__).with_name('scada.db')))
MUESTRA_S = float(os.environ.get('MUESTRA_S', '2'))   # cada cuánto se guarda una muestra de scara/estado
T_MIN, T_MAX = 20.0, 50.0                              # ventana de tiempo de ciclo de la estación
FRONTEND = Path(__file__).resolve().parent.parent / 'frontend'


# ---------- Base de datos ----------
db = sqlite3.connect(DB_PATH, check_same_thread=False)
db_lock = threading.Lock()
db.executescript('''
CREATE TABLE IF NOT EXISTS muestras (ts REAL, ciclo TEXT, temp_pico REAL, temp_stm32 REAL,
    i1 REAL, i2 REAL, i3 REAL, u1 REAL, u2 REAL, u3 REAL, pos1 REAL, pos2 REAL, pos3 REAL);
CREATE TABLE IF NOT EXISTS eventos (ts REAL, eje TEXT, falla TEXT, i REAL);
CREATE TABLE IF NOT EXISTS ciclos (ts REAL, duracion REAL);
CREATE INDEX IF NOT EXISTS muestras_ts ON muestras (ts);
''')


def sql(query, args=()):
    with db_lock:
        cur = db.execute(query, args)
        db.commit()
        return cur.fetchall()


# ---------- Estado en memoria y clientes WebSocket ----------
ultimo = {'estado': None, 'est1': None}
recibido = {'estado': 0.0, 'est1': 0.0}
ultima_muestra = 0.0
ultimos_ciclos = None
clientes: set[WebSocket] = set()
loop: asyncio.AbstractEventLoop | None = None


async def difundir(msg: str):
    for ws in list(clientes):
        try:
            await ws.send_text(msg)
        except Exception:
            clientes.discard(ws)


def on_message(_client, _userdata, m):
    global ultima_muestra, ultimos_ciclos
    topic = m.topic[len(PREFIX) + 1:]
    try:
        data = json.loads(m.payload)
    except ValueError:
        return
    ahora = time.time()

    if topic == 'estado':
        ultimo['estado'], recibido['estado'] = data, ahora
        if ahora - ultima_muestra >= MUESTRA_S:
            ultima_muestra = ahora
            ejes = (data.get('ejes') or []) + [{}] * 3
            temp = data.get('temp') or {}
            sql('INSERT INTO muestras VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)',
                (ahora, data.get('ciclo'), temp.get('pico'), temp.get('stm32'),
                 *(e.get('i') for e in ejes[:3]), *(e.get('u') for e in ejes[:3]), *(e.get('pos') for e in ejes[:3])))
    elif topic == 'evento':
        sql('INSERT INTO eventos VALUES (?,?,?,?)', (ahora, data.get('eje'), data.get('falla'), data.get('i')))
    elif topic == 'est1':
        ultimo['est1'], recibido['est1'] = data, ahora
        n = data.get('ciclos')
        # Un ciclo nuevo terminó cuando sube el contador (el primer mensaje solo fija la referencia)
        if isinstance(n, int) and ultimos_ciclos is not None and n > ultimos_ciclos and data.get('t_ultimo'):
            sql('INSERT INTO ciclos VALUES (?,?)', (ahora, data['t_ultimo']))
        if isinstance(n, int):
            ultimos_ciclos = n
    else:
        return

    if loop is not None:
        msg = json.dumps({'topic': topic, 'data': data})
        asyncio.run_coroutine_threadsafe(difundir(msg), loop)


def iniciar_mqtt():
    try:
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)   # paho 2.x
    except AttributeError:
        c = mqtt.Client()                                    # paho 1.x
    c.on_connect = lambda cl, *_: cl.subscribe([(f'{PREFIX}/estado', 0), (f'{PREFIX}/evento', 1), (f'{PREFIX}/est1', 0)])
    c.on_message = on_message
    c.reconnect_delay_set(1, 30)
    c.connect_async(MQTT_HOST, MQTT_PORT)
    c.loop_start()
    return c


# ---------- API ----------
@contextlib.asynccontextmanager
async def lifespan(_app):
    global loop
    loop = asyncio.get_running_loop()
    cliente = iniciar_mqtt()
    yield
    cliente.loop_stop()


app = FastAPI(title='SCADA SCARA', lifespan=lifespan)


@app.get('/api/estado')
def api_estado():
    ahora = time.time()
    edad = lambda k: round(ahora - recibido[k], 1) if recibido[k] else None
    return {'estado': ultimo['estado'], 'est1': ultimo['est1'],
            'edad_estado': edad('estado'), 'edad_est1': edad('est1'), 'broker': f'{MQTT_HOST}:{MQTT_PORT}'}


@app.get('/api/eventos')
def api_eventos(n: int = 50):
    rows = sql('SELECT ts, eje, falla, i FROM eventos ORDER BY ts DESC LIMIT ?', (n,))
    return [{'ts': r[0], 'eje': r[1], 'falla': r[2], 'i': r[3]} for r in rows]


@app.get('/api/ciclos')
def api_ciclos(n: int = 50):
    rows = sql('SELECT ts, duracion FROM ciclos ORDER BY ts DESC LIMIT ?', (n,))
    dur = [r[1] for r in rows]
    return {
        'ciclos': [{'ts': r[0], 'duracion': r[1], 'en_ventana': T_MIN <= r[1] <= T_MAX} for r in rows],
        'promedio': round(sum(dur) / len(dur), 1) if dur else None,
        'en_ventana': sum(T_MIN <= d <= T_MAX for d in dur),
        'total': len(dur),
    }


@app.get('/api/historial')
def api_historial(min: float = 10):
    rows = sql('SELECT * FROM muestras WHERE ts >= ? ORDER BY ts', (time.time() - min * 60,))
    cols = ['ts', 'ciclo', 'temp_pico', 'temp_stm32', 'i1', 'i2', 'i3', 'u1', 'u2', 'u3', 'pos1', 'pos2', 'pos3']
    return [dict(zip(cols, r)) for r in rows]


@app.websocket('/ws')
async def ws(websocket: WebSocket):
    await websocket.accept()
    clientes.add(websocket)
    # Al conectarse, mandar lo último que se tiene para no esperar al próximo mensaje
    for topic in ('estado', 'est1'):
        if ultimo[topic] is not None:
            await websocket.send_text(json.dumps({'topic': topic, 'data': ultimo[topic]}))
    try:
        while True:
            await websocket.receive_text()   # la página no manda comandos; esto solo detecta el cierre
    except WebSocketDisconnect:
        clientes.discard(websocket)


if FRONTEND.is_dir():
    app.mount('/', StaticFiles(directory=FRONTEND, html=True), name='frontend')
