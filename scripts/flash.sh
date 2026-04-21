#!/usr/bin/env bash
# Linux equivalent of flash.ps1: loops through reset strategies until one works.
# Usage: ./scripts/flash.sh [--port /dev/ttyUSB0] [--baud 115200] [--max-attempts 30]

set -u

PORT="/dev/ttyUSB0"
BAUD=115200
PROJECT_NAME="esp32_docker_starter"
MAX_ATTEMPTS=30

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)         PORT="$2"; shift 2 ;;
        --baud)         BAUD="$2"; shift 2 ;;
        --max-attempts) MAX_ATTEMPTS="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--port PORT] [--baud BAUD] [--max-attempts N]"
            exit 0
            ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BOOTLOADER="$BUILD_DIR/bootloader/bootloader.bin"
PARTITION_TABLE="$BUILD_DIR/partition_table/partition-table.bin"
APP="$BUILD_DIR/$PROJECT_NAME.bin"

for f in "$BOOTLOADER" "$PARTITION_TABLE" "$APP"; do
    if [[ ! -f "$f" ]]; then
        echo "Missing build artifact: $f" >&2
        exit 1
    fi
done

if [[ $BAUD -gt 115200 ]]; then
    RETRY_BAUD=115200
else
    RETRY_BAUD=$BAUD
fi

SLOW_RESET="D0|R1|W0.1|D1|R0|W0.5|D0|W0.5"
SWAPPED_RESET="R0|D1|W0.1|R1|D0|W0.5|R0|W0.5"

# Strategies cycled through on each attempt: "baud|reset-sequence|label"
STRATEGIES=(
    "$BAUD|$SLOW_RESET|slow reset"
    "$RETRY_BAUD|$SLOW_RESET|slow reset (retry)"
    "$RETRY_BAUD|$SWAPPED_RESET|swapped-polarity reset"
)

flash_attempt() {
    local attempt_baud="$1"
    local reset_seq="$2"
    local label="$3"

    echo "Flashing $PROJECT_NAME on $PORT at $attempt_baud baud ($label)..."
    echo "Using reset sequence: $reset_seq"

    ESPTOOL_CUSTOM_RESET_SEQUENCE="$reset_seq" \
    python -m esptool \
        --chip esp32 \
        --port "$PORT" \
        --baud "$attempt_baud" \
        --before default_reset \
        --after hard_reset \
        --connect-attempts 5 \
        write_flash \
        --flash_mode dio \
        --flash_freq 40m \
        --flash_size keep \
        0x1000 "$BOOTLOADER" \
        0x8000 "$PARTITION_TABLE" \
        0x10000 "$APP"
}

capture_output=$(mktemp)
trap 'rm -f "$capture_output"' EXIT

for (( attempt=1; attempt<=MAX_ATTEMPTS; attempt++ )); do
    idx=$(( (attempt - 1) % ${#STRATEGIES[@]} ))
    IFS='|' read -r attempt_baud reset_seq label <<< "${STRATEGIES[$idx]}"

    echo ""
    echo "=== Attempt $attempt of $MAX_ATTEMPTS -- $label ==="

    if flash_attempt "$attempt_baud" "$reset_seq" "$label" 2>&1 | tee "$capture_output"; then
        echo "Flash succeeded on attempt $attempt."
        exit 0
    fi

    if grep -qE "Permission denied|could not open port|Device or resource busy" "$capture_output"; then
        echo "Serial port $PORT is busy -- aborting loop." >&2
        echo "Close any serial monitor on $PORT, then retry." >&2
        exit 1
    fi

    if (( attempt < MAX_ATTEMPTS )); then
        echo "Attempt $attempt failed. Retrying in 2s... (Ctrl+C to stop)"
        sleep 2
    fi
done

echo "All $MAX_ATTEMPTS attempts failed." >&2
if grep -qE "Wrong boot mode detected" "$capture_output"; then
    echo "ESP32 never entered download mode -- USB power / cable / auto-reset hardware issue." >&2
    echo "Manual boot:" >&2
    echo "  1. Hold BOOT" >&2
    echo "  2. Run this script" >&2
    echo "  3. Tap EN/RST while still holding BOOT" >&2
    echo "  4. Release BOOT when 'Stub flasher running' appears" >&2
fi

exit 1
