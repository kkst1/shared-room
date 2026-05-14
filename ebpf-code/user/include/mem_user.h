/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MemSnoop - eBPF Memory Analysis Engine
 * Copyright (c) 2025 kkst
 *
 * user/include/mem_user.h - User-space helper utilities and CLI parsing
 */
#ifndef __MEM_USER_H
#define __MEM_USER_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "../../include/mem_common.h"

#define DEFAULT_TOP_N      10
#define DEFAULT_INTERVAL   1
#define DEFAULT_DURATION   0

struct cli_opts {
    __u32 target_pid;
    int   top_n;
    int   interval_sec;
    int   duration_sec;
    bool  show_hist;
    bool  show_stack;
    bool  verbose;
    bool  show_leak;
    int   leak_after_sec;   /* seconds; allocations older than this are suspects */
    __u64 min_leak_size;    /* ignore allocations smaller than this */
};

struct pid_entry {
    __u32 pid;
    char  comm[TASK_COMM_LEN];
    __u64 alloc_count;
    __u64 free_count;
    __u64 total_alloc_bytes;
    __u64 total_free_bytes;
};

static const char *event_type_str(__u32 type)
{
    switch (type) {
    case EVENT_ALLOC: return "ALLOC";
    case EVENT_FREE:  return "FREE";
    default:          return "UNKNOWN";
    }
}

static const char *hist_bucket_str(int bucket)
{
    static const char *labels[] = {
        "0-64B",
        "64-256B",
        "256B-1KB",
        "1KB-4KB",
        "4KB-16KB",
        "16KB-64KB",
        "64KB-256KB",
        "256KB+",
    };

    if (bucket >= 0 && bucket < NUM_HIST_BUCKETS)
        return labels[bucket];
    return "UNKNOWN";
}

static void print_size_human(__u64 bytes, char *buf, size_t buf_sz)
{
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, buf_sz, "%.1fGB", (double)bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, buf_sz, "%.1fMB", (double)bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        snprintf(buf, buf_sz, "%.1fKB", (double)bytes / 1024.0);
    else
        snprintf(buf, buf_sz, "%" PRIu64 "B", bytes);
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "MemSnoop v0.3 - eBPF Memory Analysis Engine (by kkst)\n"
        "\n"
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --pid <pid>        Trace only the specified process\n"
        "  --top <n>          Show top N processes (default: %d)\n"
        "  --interval <sec>   Refresh interval in seconds (default: %d)\n"
        "  --duration <sec>   Run for specified seconds then exit (0=forever)\n"
        "  --hist             Show allocation size histogram\n"
        "  --stack            Show top allocation stack traces\n"
        "  --leak             Show suspected memory leaks\n"
        "  --leak-after <sec> Leak detection threshold in seconds (default: 10)\n"
        "  --min-size <bytes> Ignore allocations smaller than this (default: 0)\n"
        "  --verbose          Print every event to stdout\n"
        "  --help             Show this help message\n",
        prog, DEFAULT_TOP_N, DEFAULT_INTERVAL);
}

static int parse_opts(int argc, char **argv, struct cli_opts *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->top_n = DEFAULT_TOP_N;
    opts->interval_sec = DEFAULT_INTERVAL;
    opts->duration_sec = DEFAULT_DURATION;
    opts->leak_after_sec = 10;  /* default 10 seconds */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            opts->target_pid = (__u32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--top") == 0 && i + 1 < argc) {
            opts->top_n = atoi(argv[++i]);
            if (opts->top_n <= 0) opts->top_n = DEFAULT_TOP_N;
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            opts->interval_sec = atoi(argv[++i]);
            if (opts->interval_sec <= 0) opts->interval_sec = DEFAULT_INTERVAL;
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            opts->duration_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hist") == 0) {
            opts->show_hist = true;
        } else if (strcmp(argv[i], "--stack") == 0) {
            opts->show_stack = true;
        } else if (strcmp(argv[i], "--leak") == 0) {
            opts->show_leak = true;
        } else if (strcmp(argv[i], "--leak-after") == 0 && i + 1 < argc) {
            opts->leak_after_sec = atoi(argv[++i]);
            if (opts->leak_after_sec <= 0) opts->leak_after_sec = 10;
        } else if (strcmp(argv[i], "--min-size") == 0 && i + 1 < argc) {
            opts->min_leak_size = (__u64)atoll(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            opts->verbose = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

#endif /* __MEM_USER_H */
