// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "mem.skel.h"
#include "include/mem_user.h"

static volatile sig_atomic_t exiting = 0;

static void sig_handler(int sig)
{
    exiting = 1;
}

static int handle_event(void *ctx, void *data, size_t data_sz)
{
    const struct mem_event *e = data;
    printf("%-7s  pid=%-6u  comm=%-16s  ptr=0x%-14llx  size=%-8llu  ts=%llu\n",
           event_type_str(e->type),
           e->pid,
           e->comm,
           e->ptr,
           e->size,
           e->timestamp_ns);
    return 0;
}

int main(int argc, char **argv)
{
    struct mem_bpf *skel;
    struct ring_buffer *rb = NULL;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = mem_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = mem_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs: %d\n", err);
        goto cleanup;
    }

    rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event,
                          NULL, NULL);
    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        err = -1;
        goto cleanup;
    }

    printf("mem-analyzer started. Tracing kmalloc/kfree... Ctrl-C to stop.\n");
    printf("%-7s  %-10s  %-18s  %-18s  %-10s  %s\n",
           "TYPE", "PID", "COMM", "PTR", "SIZE", "TIMESTAMP");

    while (!exiting) {
        err = ring_buffer__poll(rb, 100);
        if (err == -EINTR) {
            err = 0;
            break;
        }
        if (err < 0) {
            fprintf(stderr, "Error polling ring buffer: %d\n", err);
            break;
        }
    }

cleanup:
    ring_buffer__free(rb);
    mem_bpf__destroy(skel);
    return err < 0 ? 1 : 0;
}
