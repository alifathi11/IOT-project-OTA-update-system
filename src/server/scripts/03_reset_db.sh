#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

rm -f storage/ota.db
rm -f storage/firmwares/*.bin

echo "Database and uploaded firmwares cleared."
