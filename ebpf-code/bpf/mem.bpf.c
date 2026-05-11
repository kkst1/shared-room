// SPDX-License-Identifier: GPL-2.0
/*
 * MemSnoop - eBPF Memory Analysis Engine
 * Copyright (c) 2025 kkst
 *
 * bpf/mem.bpf.c - Kernel-side BPF programs for memory event tracing
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "../include/mem_common.h"

/* trace_event_raw_kmalloc is missing from some kernels' BTF; define manually */
struct trace_event_raw_kmalloc {
    struct trace_entry ent;
    long unsigned int call_site;
    const void *ptr;
    size_t bytes_req;
    size_t bytes_alloc;
    gfp_t gfp_flags;
    int node;
};

/* ─── Maps ─────────────────────────────────────────────────────────────────── */

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, RINGBUF_SIZE);
} events SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_STACK_TRACE);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, MAX_STACK_DEPTH * sizeof(__u64));
    __uint(max_entries, 16384);
} stack_traces SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_PID_ENTRIES);
    __type(key, __u32);
    __type(value, struct pid_stats);
} pid_stats_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_ALLOC_ENTRIES);
    __type(key, __u64);
    __type(value, struct alloc_info);
} alloc_table SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, NUM_HIST_BUCKETS);
    __type(key, __u32);
    __type(value, __u64);
} hist_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} target_pid SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

#define STATS_EVENTS_TOTAL   0
#define STATS_EVENTS_DROPPED 1

/* ─── Helpers ──────────────────────────────────────────────────────────────── */

static __always_inline __u32 size_to_bucket(__u64 size)
{
    if (size <= 64)         return 0;
    if (size <= 256)        return 1;
    if (size <= 1024)       return 2;
    if (size <= 4096)       return 3;
    if (size <= 16384)      return 4;
    if (size <= 65536)      return 5;
    if (size <= 262144)     return 6;
    return 7;
}

static __always_inline bool should_filter(__u32 pid)
{
    __u32 key = 0;
    __u32 *target = bpf_map_lookup_elem(&target_pid, &key);

    if (target && *target != 0 && *target != pid)
        return true;
    return false;
}

static __always_inline void update_stats(__u32 idx)
{
    __u32 key = idx;
    __u64 *val = bpf_map_lookup_elem(&stats, &key);

    if (val)
        __sync_fetch_and_add(val, 1);
}

/* ─── Alloc path ───────────────────────────────────────────────────────────── */

static __always_inline int trace_alloc(void *ctx, __u64 ptr,
                                       __u64 bytes_alloc)
{
    struct mem_event *e;
    __u64 pid_tgid;
    __u32 pid, tid;

    if (bytes_alloc == 0 || !ptr)
        return 0;

    pid_tgid = bpf_get_current_pid_tgid();
    pid = pid_tgid >> 32;        /* userspace PID */
    tid = (__u32)pid_tgid;       /* kernel thread ID */

    if (should_filter(pid))
        return 0;

    /* Update per-PID stats */
    struct pid_stats *ps = bpf_map_lookup_elem(&pid_stats_map, &pid);
    if (ps) {
        __sync_fetch_and_add(&ps->alloc_count, 1);
        __sync_fetch_and_add(&ps->total_alloc_bytes, bytes_alloc);
    } else {
        struct pid_stats new_ps = {
            .alloc_count = 1,
            .free_count = 0,
            .total_alloc_bytes = bytes_alloc,
            .total_free_bytes = 0,
        };
        bpf_map_update_elem(&pid_stats_map, &pid, &new_ps, BPF_NOEXIST);
    }

    /* Update histogram */
    __u32 bucket = size_to_bucket(bytes_alloc);
    __u64 *hist_val = bpf_map_lookup_elem(&hist_map, &bucket);
    if (hist_val)
        __sync_fetch_and_add(hist_val, 1);

    /* Capture stack trace and record allocation */
    __s32 stack_id = bpf_get_stackid(ctx, &stack_traces,
                                     BPF_F_FAST_STACK_CMP | BPF_F_REUSE_STACKID);

    struct alloc_info info = {
        .size = bytes_alloc,
        .timestamp_ns = bpf_ktime_get_ns(),
        .pid = pid,
        .stack_id = stack_id,
    };
    bpf_map_update_elem(&alloc_table, &ptr, &info, BPF_ANY);

    /* Submit event to ring buffer */
    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) {
        update_stats(STATS_EVENTS_DROPPED);
        return 0;
    }

    e->pid = pid;
    e->tid = tid;
    e->size = bytes_alloc;
    e->ptr = ptr;
    e->type = EVENT_ALLOC;
    e->timestamp_ns = info.timestamp_ns;
    e->stack_id = stack_id;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    update_stats(STATS_EVENTS_TOTAL);
    return 0;
}

SEC("tracepoint/kmem/kmalloc")
int tracepoint_kmalloc(struct trace_event_raw_kmalloc *ctx)
{
    return trace_alloc(ctx, (__u64)ctx->ptr, ctx->bytes_alloc);
}

/* ─── Free path ────────────────────────────────────────────────────────────── */

static __always_inline int trace_free(void *ctx, __u64 ptr)
{
    struct mem_event *e;
    __u64 pid_tgid;
    __u32 pid, tid;

    if (!ptr)
        return 0;

    pid_tgid = bpf_get_current_pid_tgid();
    pid = pid_tgid >> 32;        /* userspace PID */
    tid = (__u32)pid_tgid;       /* kernel thread ID */

    if (should_filter(pid))
        return 0;

    /* Look up alloc_info to get size for stats update */
    struct alloc_info *info = bpf_map_lookup_elem(&alloc_table, &ptr);
    __u64 freed_size = 0;
    if (info)
        freed_size = info->size;

    /* Update per-PID stats */
    struct pid_stats *ps = bpf_map_lookup_elem(&pid_stats_map, &pid);
    if (ps) {
        __sync_fetch_and_add(&ps->free_count, 1);
        __sync_fetch_and_add(&ps->total_free_bytes, freed_size);
    }

    /* Remove from alloc_table */
    bpf_map_delete_elem(&alloc_table, &ptr);

    /* Submit event to ring buffer */
    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) {
        update_stats(STATS_EVENTS_DROPPED);
        return 0;
    }

    e->pid = pid;
    e->tid = tid;
    e->size = freed_size;
    e->ptr = ptr;
    e->type = EVENT_FREE;
    e->timestamp_ns = bpf_ktime_get_ns();
    e->stack_id = -1;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    update_stats(STATS_EVENTS_TOTAL);
    return 0;
}

SEC("tracepoint/kmem/kfree")
int tracepoint_kfree(struct trace_event_raw_kfree *ctx)
{
    return trace_free(ctx, (__u64)ctx->ptr);
}

char LICENSE[] SEC("license") = "GPL";
