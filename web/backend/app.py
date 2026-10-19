"""Backend del SCADA de la Estación 1 (SCARA RRP, Grupo 7-G).

Se suscribe al broker MQTT por TCP, guarda las fallas y los tiempos de ciclo en SQLite,
sirve la página (../frontend) y le pasa los mensajes en vivo por Server-Sent Events.

Ejecutar:  uvicorn app:app --host 0.0.0.0 --port 8000   (desde web/backend)
"""
import asyncio
import json
import os
import sqlite3
import threading
import time
from contextlib import asynccontextmanager
from pathlib import Path

import paho.mqtt.client as mqtt
from fastapi import FastAPI, Request
from fastapi.responses import FileResponse, JSONResponse, StreamingResponse

# ---------- Configuración (variables de entorno) ----------
MQTT_HOST = os.getenv("MQTT_HOST", "broker.hivemq.com")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER") or None
MQTT_PASS = os.getenv("MQTT_PASS") or None
PREFIJO = os.getenv("SCARA_PREFIJO", "scara").rstrip("/")
DB_PATH = Path(os.getenv("SCADA_DB", Path(__file__).with_name("scada.db")))
FRONTEND = Path(os.getenv("SCADA_FRONTEND", Path(__file__).resolve().parent.parent / "frontend"))

TOPICS = {"estado": f"{PREFIJO}/estado", "evento": f"{PREFIJO}/evento", "est1": f"{PREFIJO}/est1"}


# ---------- Persistencia ----------
class Historial:
    def __init__(self, path: Path):
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.lock = threading.Lock()
        with self.lock:
            self.db.executescript(
                """
                CREATE TABLE IF NOT EXISTS eventos (ts REAL, eje TEXT, falla TEXT, i REAL, up INTEGER);
                CREATE TABLE IF NOT EXISTS ciclos  (ts REAL, duracion REAL, n INTEGER);
                """
            )

    def evento(self, ev: dict):
        with self.lock:
            self.db.execute("INSERT INTO eventos VALUES (?,?,?,?,?)",
                            (time.time(), ev.get("eje"), ev.get("falla"), ev.get("i"), ev.get("up")))
            self.db.commit()

    def ciclo(self, duracion: float, n):
        with self.lock:
            self.db.execute("INSERT INTO ciclos VALUES (?,?,?)", (time.time(), duracion, n))
            self.db.commit()

    def leer(self, tabla: str, limite: int):
        cols = {"eventos": ("ts", "eje", "falla", "i", "up"), "ciclos": ("ts", "duracion", "n")}[tabla]
        with self.lock:
            filas = self.db.execute(f"SELECT {','.join(cols)} FROM {tabla} ORDER BY ts DESC LIMIT ?", (limite,)).fetchall()
        return [dict(zip(cols, f)) for f in filas]


# ---------- Puente MQTT → clientes SSE ----------
class Puente:
    def __init__(self, hist: Historial):
        self.hist = hist
        self.ultimo = {k: None for k in TOPICS}   # último mensaje de cada topic
        self.recibido = {k: None for k in TOPICS}  # hora (epoch) en que llegó
        self.broker_ok = False
        self.ultimo_ciclo = None
        self.clientes: set[asyncio.Queue] = set()
        self.loop: asyncio.AbstractEventLoop | None = None
        self.cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"scada-backend-{os.getpid()}")
        if MQTT_USER:
            self.cli.username_pw_set(MQTT_USER, MQTT_PASS)
        self.cli.on_connect = self._on_connect
        self.cli.on_disconnect = self._on_disconnect
        self.cli.on_message = self._on_message
        self.cli.reconnect_delay_set(1, 10)

    def arrancar(self, loop):
        self.loop = loop
        self.cli.connect_async(MQTT_HOST, MQTT_PORT, keepalive=30)
        self.cli.loop_start()

    def parar(self):
        self.cli.loop_stop()
        self.cli.disconnect()

    # Callbacks del hilo de paho
    def _on_connect(self, cli, _ud, _flags, rc, _props=None):
        self.broker_ok = not rc.is_failure
        if self.broker_ok:
            cli.subscribe([(TOPICS["estado"], 0), (TOPICS["evento"], 1), (TOPICS["est1"], 1)])
        self._emitir("broker", self.salud())

    def _on_disconnect(self, *_args):
        self.broker_ok = False
        self._emitir("broker", self.salud())

    def _on_message(self, _cli, _ud, msg):
        tipo = next((k for k, t in TOPICS.items() if t == msg.topic), None)
        if tipo is None:
            return
        try:
            datos = json.loads(msg.payload)
        except ValueError:
            return
        self.ultimo[tipo] = datos
        self.recibido[tipo] = time.time()
        if tipo == "evento":
            self.hist.evento(datos)
        if tipo == "est1" or (tipo == "estado" and isinstance(datos.get("est1"), dict)):
            st = datos if tipo == "est1" else datos["est1"]
            dur = st.get("t_ultimo")
            if isinstance(dur, (int, float)) and dur > 0 and dur != self.ultimo_ciclo:
                if not msg.retain:  # un retain es un ciclo viejo que ya se guardó
                    self.hist.ciclo(dur, st.get("ciclos"))
                self.ultimo_ciclo = dur
        self._emitir(tipo, datos)

    def _emitir(self, tipo, datos):
        if not self.loop:
            return
        linea = f"event: {tipo}\ndata: {json.dumps(datos, separators=(',', ':'))}\n\n"
        for q in list(self.clientes):
            self.loop.call_soon_threadsafe(self._poner, q, linea)

    @staticmethod
    def _poner(q: asyncio.Queue, linea: str):
        if q.full():  # cliente lento: se descarta lo más viejo
            q.get_nowait()
        q.put_nowait(linea)

    def salud(self):
        return {"broker": f"{MQTT_HOST}:{MQTT_PORT}", "prefijo": PREFIJO, "broker_ok": self.broker_ok,
                "recibido": self.recibido, "clientes": len(self.clientes)}


hist = Historial(DB_PATH)
puente = Puente(hist)


@asynccontextmanager
async def vida(_app):
    puente.arrancar(asyncio.get_running_loop())
    yield
    puente.parar()


app = FastAPI(title="SCADA Estación 1", lifespan=vida)


@app.get("/api/salud")
def api_salud():
    return puente.salud()


@app.get("/api/actual")
def api_actual():
    return {"estado": puente.ultimo["estado"], "est1": puente.ultimo["est1"], "recibido": puente.recibido}


@app.get("/api/eventos")
def api_eventos(limite: int = 50):
    return hist.leer("eventos", max(1, min(limite, 500)))


@app.get("/api/ciclos")
def api_ciclos(limite: int = 50):
    return hist.leer("ciclos", max(1, min(limite, 500)))


@app.get("/api/stream")
async def api_stream(request: Request):
    q: asyncio.Queue = asyncio.Queue(maxsize=200)
    puente.clientes.add(q)

    async def flujo():
        try:
            yield f"event: broker\ndata: {json.dumps(puente.salud())}\n\n"
            for tipo in ("est1", "estado"):  # estado inicial para que la página no arranque vacía
                if puente.ultimo[tipo] is not None:
                    yield f"event: {tipo}\ndata: {json.dumps(puente.ultimo[tipo])}\n\n"
            while not await request.is_disconnected():
                try:
                    yield await asyncio.wait_for(q.get(), timeout=15)
                except asyncio.TimeoutError:
                    yield ": ping\n\n"
        finally:
            puente.clientes.discard(q)

    return StreamingResponse(flujo(), media_type="text/event-stream", headers={"Cache-Control": "no-cache"})


@app.get("/")
def index():
    pagina = FRONTEND / "index.html"
    if not pagina.exists():
        return JSONResponse({"error": f"No se encontró {pagina}"}, status_code=500)
    return FileResponse(pagina)
