from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import RedirectResponse
from fastapi.staticfiles import StaticFiles

from .config import PANEL_DIR
from .db import init_db
from .routers import devices, firmwares, updates


@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    yield


app = FastAPI(title="OTA Management Server", version="0.1.0", lifespan=lifespan)

# Open during development so the panel can also be served from a separate port.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(devices.router)
app.include_router(firmwares.router)
app.include_router(updates.router)


@app.get("/api/health")
def health():
    return {"status": "ok"}


@app.get("/", include_in_schema=False)
def root():
    return RedirectResponse("/panel/")


if PANEL_DIR.is_dir():
    app.mount("/panel", StaticFiles(directory=PANEL_DIR, html=True), name="panel")
