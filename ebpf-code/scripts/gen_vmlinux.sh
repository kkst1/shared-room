#!/bin/bash
# Generate vmlinux.h from running kernel's BTF
set -e

VMLINUX_H="vmlinux.h"
BTF_FILE="/sys/kernel/btf/vmlinux"

if [ ! -f "$BTF_FILE" ]; then
    echo "ERROR: $BTF_FILE not found. Kernel BTF support required."
    echo "Check: CONFIG_DEBUG_INFO_BTF=y in kernel config"
    exit 1
fi

echo "Generating $VMLINUX_H from $BTF_FILE ..."
bpftool btf dump file "$BTF_FILE" format c > "$VMLINUX_H"
echo "Done. $(wc -l < "$VMLINUX_H") lines generated."
