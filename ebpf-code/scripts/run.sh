#!/bin/bash
# Run mem-analyzer with default settings
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$SCRIPT_DIR/../output/mem-analyzer"

if [ ! -f "$BIN" ]; then
    echo "Binary not found. Run 'make' first."
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "eBPF requires root. Re-running with sudo..."
    exec sudo "$BIN" "$@"
fi

exec "$BIN" "$@"
