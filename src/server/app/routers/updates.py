import sqlite3
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException

from ..db import get_db
from ..helpers import get_device_or_404
from ..schemas import StatusIn

router = APIRouter(prefix="/api/updates", tags=["updates"])

TERMINAL = ("success", "failed", "rolled_back")


@router.post("/{update_id}/status")
def report_status(update_id: int, body: StatusIn,
                  conn: sqlite3.Connection = Depends(get_db)):
    """Every OTA step the board goes through lands here as one event row."""
    job = conn.execute(
        """SELECT j.*, f.version AS target_version
           FROM update_jobs j JOIN firmwares f ON f.id = j.target_firmware_id
           WHERE j.id = ?""",
        (update_id,),
    ).fetchone()
    if job is None:
        raise HTTPException(status_code=404, detail="update job not found")
    if job["status"] in TERMINAL:
        raise HTTPException(status_code=409, detail="update job is already closed")

    conn.execute(
        "INSERT INTO update_events (update_id, status, message) VALUES (?, ?, ?)",
        (update_id, body.status, body.message),
    )
    conn.execute(
        """UPDATE update_jobs
           SET status = ?,
               started_at = COALESCE(started_at, CURRENT_TIMESTAMP),
               finished_at = CASE WHEN ? IN ('success', 'failed', 'rolled_back')
                                  THEN CURRENT_TIMESTAMP ELSE finished_at END,
               error_message = CASE WHEN ? = 'failed' THEN ? ELSE error_message END
           WHERE id = ?""",
        (body.status, body.status, body.status, body.message, update_id),
    )

    # The device version only really changes once install succeeds or we roll back.
    new_version = None
    if body.status == "success":
        new_version = job["target_version"]
    elif body.status == "rolled_back":
        new_version = job["from_version"]

    conn.execute(
        """UPDATE devices
           SET last_update_status = ?,
               current_version = COALESCE(?, current_version),
               last_seen_at = CURRENT_TIMESTAMP,
               updated_at = CURRENT_TIMESTAMP
           WHERE id = ?""",
        (body.status, new_version, job["device_id"]),
    )
    return {"update_id": update_id, "status": body.status}


@router.get("")
def list_updates(device_id: Optional[str] = None,
                 conn: sqlite3.Connection = Depends(get_db)):
    """Update history, optionally narrowed to one device."""
    sql = """SELECT j.*, d.device_uid, f.version AS target_version
             FROM update_jobs j
             JOIN devices d   ON d.id = j.device_id
             JOIN firmwares f ON f.id = j.target_firmware_id"""
    params: tuple = ()
    if device_id:
        device = get_device_or_404(conn, device_id)
        sql += " WHERE j.device_id = ?"
        params = (device["id"],)
    sql += " ORDER BY j.requested_at DESC, j.id DESC"

    jobs = [dict(r) for r in conn.execute(sql, params).fetchall()]
    for job in jobs:
        job["events"] = [
            dict(e) for e in conn.execute(
                "SELECT status, message, created_at FROM update_events "
                "WHERE update_id = ? ORDER BY created_at, id",
                (job["id"],),
            ).fetchall()
        ]
    return jobs
