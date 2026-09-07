import sqlite3

from fastapi import APIRouter, Depends, HTTPException, Request

from ..db import get_db
from ..helpers import get_device_or_404, is_newer, version_key
from ..schemas import ManualUpdateIn, ModeIn, RegisterIn

router = APIRouter(prefix="/api/devices", tags=["devices"])

OPEN_STATES = ("pending", "downloading", "verified", "installing")
AUTO_BLOCKED_STATES = ("failed", "rolled_back")


def _job_payload(request: Request, job: sqlite3.Row) -> dict:
    return {
        "update_available": True,
        "update_id": job["id"],
        "from_version": job["from_version"],
        "target_version": job["version"],
        "firmware_id": job["target_firmware_id"],
        "file_url": str(
            request.url_for(
                "download_firmware",
                firmware_id=job["target_firmware_id"],
            )
        ),
        "file_size": job["file_size"],
        "sha256": job["sha256"],
    }


def _open_job(conn: sqlite3.Connection, device_id: int):
    return conn.execute(
        f"""SELECT j.*, f.version, f.sha256, f.file_size
            FROM update_jobs j
            JOIN firmwares f ON f.id = j.target_firmware_id
            WHERE j.device_id = ?
              AND j.status IN ({",".join("?" * len(OPEN_STATES))})
            ORDER BY j.requested_at DESC, j.id DESC
            LIMIT 1""",
        (device_id, *OPEN_STATES),
    ).fetchone()


def _select_automatic_target(
    conn: sqlite3.Connection,
    device_id: int,
    current_version: str,
):
    """
    Return the newest active firmware that is newer than the running version
    and has not already failed/rolled back on this device.
    """
    blocked_ids = {
        row["target_firmware_id"]
        for row in conn.execute(
            f"""SELECT DISTINCT target_firmware_id
                FROM update_jobs
                WHERE device_id = ?
                  AND status IN ({",".join("?" * len(AUTO_BLOCKED_STATES))})""",
            (device_id, *AUTO_BLOCKED_STATES),
        ).fetchall()
    }

    active = conn.execute(
        """SELECT *
           FROM firmwares
           WHERE is_active = 1"""
    ).fetchall()

    candidates = [
        firmware
        for firmware in active
        if firmware["id"] not in blocked_ids
        and is_newer(firmware["version"], current_version)
    ]

    if not candidates:
        return None

    return max(
        candidates,
        key=lambda firmware: version_key(firmware["version"]),
    )


@router.post("/register")
def register_device(
    body: RegisterIn,
    conn: sqlite3.Connection = Depends(get_db),
):
    """Called by the board on boot and as a periodic heartbeat."""
    existing = conn.execute(
        "SELECT id FROM devices WHERE device_uid = ?",
        (body.device_uid,),
    ).fetchone()

    if existing is None:
        conn.execute(
            """INSERT INTO devices (
                   device_uid,
                   current_version,
                   update_mode,
                   last_seen_at
               )
               VALUES (?, ?, ?, CURRENT_TIMESTAMP)""",
            (
                body.device_uid,
                body.current_version,
                body.update_mode or "manual",
            ),
        )
    else:
        # Mode is owned by the panel, so only overwrite it if the board sent one.
        conn.execute(
            """UPDATE devices
               SET current_version = ?,
                   update_mode = COALESCE(?, update_mode),
                   last_seen_at = CURRENT_TIMESTAMP,
                   updated_at = CURRENT_TIMESTAMP
               WHERE id = ?""",
            (
                body.current_version,
                body.update_mode,
                existing["id"],
            ),
        )

    device = get_device_or_404(
        conn,
        body.device_uid,
    )

    return {
        "id": device["id"],
        "device_uid": device["device_uid"],
        "current_version": device["current_version"],
        "update_mode": device["update_mode"],
        "registered": existing is None,
    }


@router.get("")
def list_devices(
    conn: sqlite3.Connection = Depends(get_db),
):
    rows = conn.execute(
        """SELECT d.*,
                  (
                      SELECT COUNT(*)
                      FROM update_jobs j
                      WHERE j.device_id = d.id
                  ) AS jobs_count
           FROM devices d
           ORDER BY d.id"""
    ).fetchall()

    return [dict(row) for row in rows]


@router.get("/{ident}/update")
def check_update(
    ident: str,
    request: Request,
    version: str = "",
    conn: sqlite3.Connection = Depends(get_db),
):
    """Board polls this to ask whether it should update."""
    device = get_device_or_404(
        conn,
        ident,
    )

    current = version or device["current_version"]

    conn.execute(
        """UPDATE devices
           SET current_version = ?,
               last_seen_at = CURRENT_TIMESTAMP,
               updated_at = CURRENT_TIMESTAMP
           WHERE id = ?""",
        (
            current,
            device["id"],
        ),
    )

    # Existing manual/automatic job always has priority.
    job = _open_job(
        conn,
        device["id"],
    )

    # No queued job + automatic mode => create one from the newest valid candidate.
    if job is None and device["update_mode"] == "automatic":
        target = _select_automatic_target(
            conn,
            device["id"],
            current,
        )

        if target is not None:
            cur = conn.execute(
                """INSERT INTO update_jobs (
                       device_id,
                       target_firmware_id,
                       from_version,
                       trigger_type,
                       status
                   )
                   VALUES (?, ?, ?, 'automatic', 'pending')""",
                (
                    device["id"],
                    target["id"],
                    current,
                ),
            )

            conn.execute(
                """INSERT INTO update_events (
                       update_id,
                       status,
                       message
                   )
                   VALUES (?, 'pending', ?)""",
                (
                    cur.lastrowid,
                    f"automatic update to {target['version']}",
                ),
            )

            conn.execute(
                """UPDATE devices
                   SET last_update_status = 'pending',
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                (device["id"],),
            )

            job = _open_job(
                conn,
                device["id"],
            )

    if job is None:
        return {
            "update_available": False,
            "current_version": current,
        }

    return _job_payload(
        request,
        job,
    )


@router.post("/{ident}/update")
def create_update(
    ident: str,
    body: ManualUpdateIn,
    conn: sqlite3.Connection = Depends(get_db),
):
    """Panel asks for a specific firmware to be pushed to one device."""
    device = get_device_or_404(
        conn,
        ident,
    )

    firmware = conn.execute(
        "SELECT * FROM firmwares WHERE id = ?",
        (body.firmware_id,),
    ).fetchone()

    if firmware is None:
        raise HTTPException(
            status_code=404,
            detail="firmware not found",
        )

    if _open_job(conn, device["id"]) is not None:
        raise HTTPException(
            status_code=409,
            detail="device already has an open update",
        )

    cur = conn.execute(
        """INSERT INTO update_jobs (
               device_id,
               target_firmware_id,
               from_version,
               trigger_type,
               status
           )
           VALUES (?, ?, ?, 'manual', 'pending')""",
        (
            device["id"],
            firmware["id"],
            device["current_version"],
        ),
    )

    conn.execute(
        """INSERT INTO update_events (
               update_id,
               status,
               message
           )
           VALUES (?, 'pending', 'created from panel')""",
        (cur.lastrowid,),
    )

    conn.execute(
        """UPDATE devices
           SET last_update_status = 'pending',
               updated_at = CURRENT_TIMESTAMP
           WHERE id = ?""",
        (device["id"],),
    )

    return {
        "update_id": cur.lastrowid,
        "device_id": device["id"],
        "target_version": firmware["version"],
        "status": "pending",
    }


@router.patch("/{ident}/mode")
def set_mode(
    ident: str,
    body: ModeIn,
    conn: sqlite3.Connection = Depends(get_db),
):
    device = get_device_or_404(
        conn,
        ident,
    )

    conn.execute(
        """UPDATE devices
           SET update_mode = ?,
               updated_at = CURRENT_TIMESTAMP
           WHERE id = ?""",
        (
            body.update_mode,
            device["id"],
        ),
    )

    return {
        "id": device["id"],
        "update_mode": body.update_mode,
    }
