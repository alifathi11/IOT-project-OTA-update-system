#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

HOST="${OTA_HOST:-0.0.0.0}"
PORT="${OTA_PORT:-8000}"

cd "$PROJECT_ROOT"

if [ ! -d ".venv" ]; then
    echo "Run scripts/01_setup.sh first."
    exit 1
fi

echo "Panel: http://localhost:$PORT/panel/   Docs: http://localhost:$PORT/docs"
exec ./.venv/bin/uvicorn app.main:app --host "$HOST" --port "$PORT" --reload
