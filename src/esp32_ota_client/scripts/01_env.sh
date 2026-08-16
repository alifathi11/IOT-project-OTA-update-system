#!/usr/bin/env bash 

export IDF_DIR="$HOME/.espressif/v6.0.2/esp-idf"
export ESP_PORT="${ESP_PORT:-/dev/ttyUSB0}"
export ESP_TARGET="esp32"

if [ ! -f "$IDF_DIR/export.sh" ]; then
    echo "ESP-IDF not found: $IDF_DIR"
    return 1 2>/dev/null || exit 1
fi

source "$IDF_DIR/export.sh"
