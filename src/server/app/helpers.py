import hashlib
import re
import sqlite3
from typing import Optional

from fastapi import HTTPException

CHUNK = 64 * 1024


def sha256_of(path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(CHUNK), b""):
            h.update(chunk)
    return h.hexdigest()


def version_key(version: str):
    """Turn '1.2.10' into (1, 2, 10) so versions sort numerically, not as text."""
    return tuple(int(p) for p in re.findall(r"\d+", version)) or (0,)


def is_newer(candidate: str, current: str) -> bool:
    return version_key(candidate) > version_key(current)


def find_device(conn: sqlite3.Connection, ident: str) -> Optional[sqlite3.Row]:
    """Devices are addressed by numeric id from the panel and by uid from the board."""
    if str(ident).isdigit():
        row = conn.execute("SELECT * FROM devices WHERE id = ?", (int(ident),)).fetchone()
        if row:
            return row
    return conn.execute("SELECT * FROM devices WHERE device_uid = ?", (str(ident),)).fetchone()


def get_device_or_404(conn: sqlite3.Connection, ident: str) -> sqlite3.Row:
    device = find_device(conn, ident)
    if device is None:
        raise HTTPException(status_code=404, detail="device not found")
    return device


def rows_to_dicts(rows) -> list[dict]:
    return [dict(r) for r in rows]
