#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

source "$SCRIPT_DIR/01_env.sh"

cd "$PROJECT_ROOT"

if [ ! -f "sdkconfig" ]; then
    echo "Configuring target: $ESP_TARGET"
    idf.py set-target "$ESP_TARGET"
fi

echo "Building..."
idf.py build

echo "Flashing to $ESP_PORT..."
idf.py -p "$ESP_PORT" flash

echo "Done."
