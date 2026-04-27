#!/bin/bash
# Stress test: trigger kernel memory allocations via file operations
echo "Generating kernel memory allocation pressure..."
for i in $(seq 1 1000); do
    dd if=/dev/zero of=/tmp/stress_alloc_$i bs=4096 count=10 2>/dev/null
done
echo "Cleaning up..."
rm -f /tmp/stress_alloc_*
echo "Done."
