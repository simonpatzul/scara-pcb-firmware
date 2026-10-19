# Web · SCADA de la Estación 1

| Carpeta | Qué hay |
|---|---|
| `frontend/` | La página (`index.html`). Se puede abrir sola con doble clic (modo demo o conexión directa al broker por WebSocket). Ver `frontend/LEEME.md` |
| `backend/` | Servidor en Python (FastAPI). Escucha el broker por TCP, guarda fallas y tiempos de ciclo en SQLite (`scada.db`), sirve la página y le pasa los datos en vivo |

## Correr el backend

```
cd web/backend
python -m venv .venv
.venv\Scripts\activate          # en Linux: source .venv/bin/activate
pip install -r requirements.txt
uvicorn app:app --host 0.0.0.0 --port 8000
```

Después abre http://localhost:8000. Desde ahí la página se conecta sola al servidor (arriba dice "Servidor · broker OK"). En http://localhost:8000/docs está la API.

Variables de entorno opcionales:

| Variable | Por defecto | Para qué |
|---|---|---|
| `MQTT_HOST` / `MQTT_PORT` | `broker.hivemq.com` / `1883` | broker |
| `MQTT_USER` / `MQTT_PASS` | vacío | si el broker pide usuario |
| `SCARA_PREFIJO` | `scara` | el mismo prefijo del STM32 y de ROS 2 |
| `SCADA_DB` | `backend/scada.db` | archivo del historial |

## API

| Ruta | Qué devuelve |
|---|---|
| `GET /api/salud` | broker, prefijo, si está conectado y cuándo llegó el último mensaje de cada topic |
| `GET /api/actual` | último `scara/estado` y `scara/est1` |
| `GET /api/eventos?limite=50` | últimas fallas (`scara/evento`) |
| `GET /api/ciclos?limite=50` | últimos ciclos terminados de la estación (duración en s) |
| `GET /api/stream` | en vivo, Server-Sent Events: `broker`, `estado`, `evento`, `est1` |

Topics que escucha: `<prefijo>/estado` y `<prefijo>/evento` (STM32) y `<prefijo>/est1` (PC, ROS 2). Los formatos están en `docs/` del repo.
