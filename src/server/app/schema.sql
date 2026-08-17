PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS firmwares (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    version     TEXT    UNIQUE NOT NULL,
    file_name   TEXT    NOT NULL,
    file_path   TEXT    UNIQUE NOT NULL,
    file_size   INTEGER NOT NULL,
    sha256      TEXT    NOT NULL CHECK (length(sha256) = 64),
    changelog   TEXT,
    is_active   BOOLEAN NOT NULL DEFAULT 1,
    created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS devices (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    device_uid         TEXT    UNIQUE NOT NULL,
    current_version    TEXT    NOT NULL,
    update_mode        TEXT    NOT NULL DEFAULT 'manual'
                               CHECK (update_mode IN ('manual', 'automatic')),
    last_update_status TEXT    DEFAULT 'idle',
    last_seen_at       DATETIME,
    created_at         DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at         DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS update_jobs (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id          INTEGER NOT NULL REFERENCES devices(id)   ON DELETE CASCADE,
    target_firmware_id INTEGER NOT NULL REFERENCES firmwares(id) ON DELETE CASCADE,
    from_version       TEXT    NOT NULL,
    trigger_type       TEXT    NOT NULL DEFAULT 'manual'
                               CHECK (trigger_type IN ('manual', 'automatic')),
    status             TEXT    NOT NULL DEFAULT 'pending'
                               CHECK (status IN ('pending', 'downloading', 'verified',
                                                 'installing', 'success', 'failed',
                                                 'rolled_back')),
    requested_at       DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at         DATETIME,
    finished_at        DATETIME,
    error_message      TEXT
);

CREATE TABLE IF NOT EXISTS update_events (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    update_id  INTEGER NOT NULL REFERENCES update_jobs(id) ON DELETE CASCADE,
    status     TEXT    NOT NULL,
    message    TEXT,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_devices_uid       ON devices(device_uid);
CREATE INDEX IF NOT EXISTS idx_firmwares_version ON firmwares(version);
CREATE INDEX IF NOT EXISTS idx_jobs_device       ON update_jobs(device_id, requested_at);
CREATE INDEX IF NOT EXISTS idx_events_update     ON update_events(update_id, created_at);
