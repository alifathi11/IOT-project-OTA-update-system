import sqlite3
from pathlib import Path

from .config import DB_PATH, FIRMWARE_DIR

SCHEMA_FILE = Path(__file__).resolve().parent / "schema.sql"


def connect() -> sqlite3.Connection:
    # check_same_thread is off because FastAPI may open and use the connection on
    # two different threadpool threads. Each request still gets its own connection.
    conn = sqlite3.connect(DB_PATH, check_same_thread=False, timeout=10)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA journal_mode = WAL")
    return conn


def get_db():
    """FastAPI dependency: one connection per request."""
    conn = connect()
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()


def init_db() -> None:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    conn = connect()
    try:
        conn.executescript(SCHEMA_FILE.read_text())
        conn.commit()
    finally:
        conn.close()
