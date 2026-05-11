#!/bin/bash
# MemSnoop - eBPF Memory Analysis Engine (by kkst)
# Stress test: generate diverse kernel memory allocation patterns
# Usage: ./stress_alloc.sh [duration_sec]

DURATION=${1:-10}
TMPDIR="/tmp/memsnoop_stress_$$"
mkdir -p "$TMPDIR"

cleanup() {
    rm -rf "$TMPDIR"
    kill 0 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

echo "=== MemSnoop stress test ==="
echo "Duration: ${DURATION}s"
echo ""

# Pattern 1: Small frequent allocations (triggers kmalloc small buckets)
echo "[1/4] Small frequent allocations (dd 512B x many)..."
(
    end=$((SECONDS + DURATION))
    i=0
    while [ $SECONDS -lt $end ]; do
        dd if=/dev/zero of="$TMPDIR/small_$i" bs=512 count=1 2>/dev/null
        i=$((i + 1))
    done
) &

# Pattern 2: Large allocations (triggers higher-order pages)
echo "[2/4] Large allocations (dd 64KB blocks)..."
(
    end=$((SECONDS + DURATION))
    i=0
    while [ $SECONDS -lt $end ]; do
        dd if=/dev/zero of="$TMPDIR/large_$i" bs=65536 count=4 2>/dev/null
        i=$((i + 1))
        sleep 0.1
    done
) &

# Pattern 3: Rapid create/delete (alloc+free churn)
echo "[3/4] Rapid file create/delete (alloc+free churn)..."
(
    end=$((SECONDS + DURATION))
    while [ $SECONDS -lt $end ]; do
        for j in $(seq 1 50); do
            touch "$TMPDIR/churn_$j"
        done
        rm -f "$TMPDIR"/churn_*
    done
) &

# Pattern 4: Network socket allocations
echo "[4/4] Network socket allocations (curl localhost)..."
(
    end=$((SECONDS + DURATION))
    while [ $SECONDS -lt $end ]; do
        curl -s -o /dev/null http://localhost/ 2>/dev/null || true
        sleep 0.2
    done
) &

echo ""
echo "All patterns running for ${DURATION}s..."
sleep "$DURATION"

echo ""
echo "=== MemSnoop stress test complete ==="
