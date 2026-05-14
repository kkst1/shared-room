/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MemSnoop - eBPF Memory Analysis Engine
 * Copyright (c) 2025 kkst
 *
 * include/mem_common.h - Shared definitions between BPF and user space
 */
#ifndef __MEM_COMMON_H
#define __MEM_COMMON_H

#ifdef __BPF__
#include "vmlinux.h"
#else
#include <linux/types.h>
#endif

#define TASK_COMM_LEN    16
#define MAX_STACK_DEPTH  32
#define MAX_PID_ENTRIES  10240
#define MAX_ALLOC_ENTRIES 65536
#define RINGBUF_SIZE     (4 * 1024 * 1024)
#define NUM_HIST_BUCKETS 8

#define STATS_EVENTS_TOTAL   0
#define STATS_EVENTS_DROPPED 1

enum event_type {
    EVENT_ALLOC = 1,
    EVENT_FREE  = 2,
};

struct mem_event {
    __u32 pid;          /* userspace PID (kernel tgid) */
    __u32 tid;          /* kernel thread ID */
    char  comm[TASK_COMM_LEN];
    __u64 size;
    __u64 ptr;
    __u64 timestamp_ns;
    __s32 stack_id;
    __u32 type;
};

struct pid_stats {
    __u64 alloc_count;
    __u64 free_count;
    __u64 total_alloc_bytes;
    __u64 total_free_bytes;
};

struct alloc_info {
    __u64 size;
    __u64 timestamp_ns;
    __u32 pid;
    __s32 stack_id;
};

#endif /* __MEM_COMMON_H */
