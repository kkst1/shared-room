// SPDX-License-Identifier: GPL-2.0
/*
 * MemSnoop - eBPF Memory Analysis Engine
 * Copyright (c) 2025 kkst
 *
 * user/main.c - User-space event consumer, periodic reporting, and CLI
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <inttypes.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "mem.skel.h"
#include "include/mem_user.h"
#include "include/stats.h"

static volatile sig_atomic_t exiting;

static void sig_handler(int sig)
{
    exiting = 1;
}

static struct cli_opts opts;

static int handle_event(void *ctx, void *data, size_t data_sz)
{
    const struct mem_event *e = data;

    if (data_sz < sizeof(*e))
        return 0;

    if (!opts.verbose)
        return 0;

    printf("%-7s  pid=%-6u  tid=%-6u  comm=%-16s  ptr=0x%-14" PRIx64
           "  size=%-8" PRIu64 "  stack_id=%-6d\n",
           event_type_str(e->type),
           e->pid,
           e->tid,
           e->comm,
           e->ptr,
           e->size,
           e->stack_id);
    return 0;
}

static void print_separator(void)
{
    printf("────────────────────────────────────────────────────────────────\n");
}

static void print_report(struct mem_bpf *skel)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timebuf[64];

    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);

    printf("\n");
    print_separator();
    printf("  MemSnoop report  [%s]\n", timebuf);
    if (opts.target_pid)
        printf("  Filter: pid=%u\n", opts.target_pid);
    print_separator();

    printf("\n  [Top %d Processes by Active Memory]\n\n", opts.top_n);
    print_pid_stats_top(bpf_map__fd(skel->maps.pid_stats_map), opts.top_n);

    if (opts.show_hist) {
        printf("\n  [Allocation Size Histogram]\n\n");
        print_histogram(bpf_map__fd(skel->maps.hist_map));
    }

    if (opts.show_stack) {
        printf("\n  [Top %d Allocation Stacks (by active bytes)]\n\n", opts.top_n);
        print_stack_top(bpf_map__fd(skel->maps.stack_traces),
                        bpf_map__fd(skel->maps.alloc_table),
                        opts.top_n);
    }

    printf("\n  [Ring Buffer Stats]\n");
    print_drop_stats(bpf_map__fd(skel->maps.stats));

    print_separator();
    printf("\n");
}

int main(int argc, char **argv)
{
    struct mem_bpf *skel;
    struct ring_buffer *rb = NULL;
    int err;

    if (parse_opts(argc, argv, &opts) < 0)
        return 1;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

    skel = mem_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = mem_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF programs: %d\n", err);
        goto cleanup;
    }

    /* Set PID filter if specified */
    if (opts.target_pid) {
        __u32 key = 0;
        __u32 pid = opts.target_pid;
        bpf_map_update_elem(bpf_map__fd(skel->maps.target_pid),
                            &key, &pid, BPF_ANY);
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

    printf("MemSnoop v0.2 - eBPF Memory Analysis Engine (by kkst)\n");
    printf("Tracing kmalloc/kfree...\n");
    printf("  interval=%ds  top=%d  pid=%u  hist=%s  stack=%s\n",
           opts.interval_sec, opts.top_n, opts.target_pid,
           opts.show_hist ? "on" : "off",
           opts.show_stack ? "on" : "off");
    printf("Press Ctrl-C to stop.\n");

    time_t start_time = time(NULL);
    time_t last_report = 0;

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

        time_t now = time(NULL);

        if (now - last_report >= opts.interval_sec) {
            print_report(skel);
            last_report = now;
        }

        if (opts.duration_sec > 0 && (now - start_time) >= opts.duration_sec) {
            printf("\nDuration reached (%ds). Exiting.\n", opts.duration_sec);
            break;
        }
    }

    /* Final report */
    if (!err || err == -EINTR) {
        printf("\n--- Final Report ---\n");
        print_report(skel);
    }

cleanup:
    ring_buffer__free(rb);
    mem_bpf__destroy(skel);
    return err < 0 ? 1 : 0;
}
