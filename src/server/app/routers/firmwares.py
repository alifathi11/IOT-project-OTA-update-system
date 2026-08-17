import re
import shutil
import sqlite3

from fastapi import APIRouter, Depends, File, Form, HTTPException, UploadFile
from fastapi.responses import FileResponse

from ..config import FIRMWARE_DIR
from ..db import get_db
from ..helpers import sha256_of, version_key

router = APIRouter(prefix="/api/firmwares", tags=["firmwares"])

VERSION_RE = re.compile(r"^\d+(\.\d+){0,3}$")


@router.get("")
def list_firmwares(conn: sqlite3.Connection = Depends(get_db)):
    rows = conn.execute("SELECT * FROM firmwares").fetchall()
    rows = sorted(rows, key=lambda r: version_key(r["version"]), reverse=True)
    return [dict(r) for r in rows]


@router.post("", status_code=201)
def upload_firmware(
    file: UploadFile = File(...),
    version: str = Form(...),
    changelog: str = Form(""),
    conn: sqlite3.Connection = Depends(get_db),
):
    """Panel uploads a .bin; the hash is computed here so the board can verify it."""
    version = version.strip()
    if not file.filename or not file.filename.lower().endswith(".bin"):
        raise HTTPException(status_code=400, detail="firmware file must be a .bin")
    if not VERSION_RE.match(version):
        raise HTTPException(status_code=400, detail="version must look like 1.0.0")
    if conn.execute("SELECT 1 FROM firmwares WHERE version = ?", (version,)).fetchone():
        raise HTTPException(status_code=409, detail="version already exists")

    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    target = FIRMWARE_DIR / f"firmware_{version}.bin"
    with open(target, "wb") as out:
        shutil.copyfileobj(file.file, out)

    size = target.stat().st_size
    if size == 0:
        target.unlink()
        raise HTTPException(status_code=400, detail="uploaded file is empty")

    cur = conn.execute(
        """INSERT INTO firmwares (version, file_name, file_path, file_size, sha256, changelog)
           VALUES (?, ?, ?, ?, ?, ?)""",
        (version, file.filename or target.name, str(target), size,
         sha256_of(target), changelog.strip() or None),
    )
    row = conn.execute("SELECT * FROM firmwares WHERE id = ?", (cur.lastrowid,)).fetchone()
    return dict(row)


@router.get("/{firmware_id}/download", name="download_firmware")
def download_firmware(firmware_id: int, conn: sqlite3.Connection = Depends(get_db)):
    row = conn.execute("SELECT * FROM firmwares WHERE id = ?", (firmware_id,)).fetchone()
    if row is None:
        raise HTTPException(status_code=404, detail="firmware not found")

    path = FIRMWARE_DIR / f"firmware_{row['version']}.bin"
    if not path.exists():
        raise HTTPException(status_code=410, detail="firmware file is missing on disk")

    return FileResponse(
        path,
        media_type="application/octet-stream",
        filename=row["file_name"],
        headers={"X-Firmware-SHA256": row["sha256"]},
    )
