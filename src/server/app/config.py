import os
from pathlib import Path

SERVER_DIR = Path(__file__).resolve().parent.parent
REPO_SRC_DIR = SERVER_DIR.parent

DB_PATH = Path(os.getenv("OTA_DB_PATH", SERVER_DIR / "storage" / "ota.db"))
FIRMWARE_DIR = Path(os.getenv("OTA_FIRMWARE_DIR", SERVER_DIR / "storage" / "firmwares"))
PANEL_DIR = Path(os.getenv("OTA_PANEL_DIR", REPO_SRC_DIR / "web_panel"))

# Devices that stay silent longer than this are shown as offline in the panel.
OFFLINE_AFTER_SECONDS = int(os.getenv("OTA_OFFLINE_AFTER", "120"))
