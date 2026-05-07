// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "../include/mem_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");

static __always_inline int trace_alloc(void *ctx, __u64 ptr,
                                       size_t bytes_alloc)
{
    struct mem_event *e;
    __u64 pid_tgid;
    __u32 pid;
    __u32 tgid;

    if (bytes_alloc == 0)
        return 0;

    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e)
        return 0;

    pid_tgid = bpf_get_current_pid_tgid();
    pid = (__u32)pid_tgid;
    tgid = pid_tgid >> 32;
    e->pid  = pid;
    e->tgid = tgid;
    e->size = bytes_alloc;
    e->ptr  = ptr;
    e->type = EVENT_ALLOC;
    e->timestamp_ns = bpf_ktime_get_ns();
    e->stack_id = -1;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tracepoint/kmem/kmalloc")
int tracepoint_kmalloc(struct trace_event_raw_kmalloc *ctx)
{
    return trace_alloc(ctx, (__u64)ctx->ptr, ctx->bytes_alloc);
}

static __always_inline int trace_free(void *ctx, __u64 ptr)
{
    struct mem_event *e;
    __u64 pid_tgid;
    __u32 pid;
    __u32 tgid;

    if (!ptr)
        return 0;

    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e)
        return 0;

    pid_tgid = bpf_get_current_pid_tgid();
    pid = (__u32)pid_tgid;
    tgid = pid_tgid >> 32;
    e->pid  = pid;
    e->tgid = tgid;
    e->size = 0;
    e->ptr  = ptr;
    e->type = EVENT_FREE;
    e->timestamp_ns = bpf_ktime_get_ns();
    e->stack_id = -1;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tracepoint/kmem/kfree")
int tracepoint_kfree(struct trace_event_raw_kfree *ctx)
{
    return trace_free(ctx, (__u64)ctx->ptr);
}

char LICENSE[] SEC("license") = "GPL";
