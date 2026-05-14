/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MemSnoop - eBPF Memory Analysis Engine
 * Copyright (c) 2025 kkst
 *
 * user/include/stats.h - Statistics display interface
 */
#ifndef __STATS_H
#define __STATS_H

#include "mem_user.h"
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#define MAX_DISPLAY_ENTRIES 128

struct stack_entry {
    __s32 stack_id;
    __u64 total_bytes;
    __u64 count;
};

/* Leak suspect: aggregated by stack_id */
struct leak_entry {
    __s32 stack_id;
    __u64 total_bytes;
    __u64 count;
    __u64 oldest_age_ns;  /* age of the oldest allocation in this group */
};

int print_pid_stats_top(int pid_stats_fd, int top_n);
int print_histogram(int hist_fd);
int print_stack_top(int stack_fd, int alloc_fd, int top_n);
void print_drop_stats(int stats_fd);

/*
 * Print suspected memory leaks.
 * Scans alloc_table for entries older than min_age_ns.
 * Groups by stack_id, sorts by total_bytes descending.
 * stack_fd: fd of stack_traces map (for resolving addresses)
 * alloc_fd: fd of alloc_table map
 */
int print_leak_suspects(int stack_fd, int alloc_fd,
                        __u64 min_age_ns, __u64 min_size, int top_n);

#endif /* __STATS_H */
