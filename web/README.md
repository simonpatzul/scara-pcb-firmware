# Web · página SCADA

| Carpeta | Qué hay |
|---|---|
| `frontend/` | La página (`index.html`). Funciona sola: se abre en el navegador y lee el broker MQTT por WebSocket. Ver `frontend/LEEME.md` |
| `backend/` | Servidor en Python (FastAPI). Se suscribe al broker, guarda el historial en SQLite (`scada.db`) y sirve la página |

## Correr el backend

```
cd web/backend
python -m venv .venv
.venv\Scripts\activate          # en Linux: source .venv/bin/activate
pip install -r requirements.txt
uvicorn app:app --host 0.0.0.0 --port 8000
```

Después se abre http://localhost:8000 (la página) o http://localhost:8000/docs (la API).

Variables de entorno opcionales: `MQTT_HOST` (por defecto `broker.hivemq.com`), `MQTT_PORT` (1883), `MQTT_PREFIX` (`scara`, el mismo que en el STM32), `DB_PATH` y `MUESTRA_S` (cada cuánto se guarda una muestra, 2 s).

## API

| Ruta | Qué devuelve |
|---|---|
| `GET /api/estado` | último `scara/estado` y `scara/est1`, con cuántos segundos tienen |
| `GET /api/eventos?n=50` | últimas fallas (`scara/evento`) |
| `GET /api/ciclos?n=50` | últimos ciclos de la Estación 1, el promedio y cuántos quedaron dentro de 20–50 s |
| `GET /api/historial?min=10` | muestras de los últimos minutos: corriente, voltaje y posición de cada eje y temperaturas |
| `WS /ws` | en vivo: `{"topic": "estado" \| "evento" \| "est1", "data": {...}}` |

Los formatos de los mensajes están en [`docs/mqtt.md`](../docs/mqtt.md).
