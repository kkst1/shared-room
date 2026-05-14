// SPDX-License-Identifier: GPL-2.0
/*
 * MemSnoop - eBPF Memory Analysis Engine
 * Copyright (c) 2025 kkst
 *
 * user/stats.c - PID statistics, histogram, stack trace, and leak detection
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "include/mem_user.h"
#include "include/stats.h"
#include "include/symbol.h"

/* ─── PID TopN ─────────────────────────────────────────────────────────────── */

static int cmp_pid_entry(const void *a, const void *b)
{
    const struct pid_entry *ea = a;
    const struct pid_entry *eb = b;
    __u64 active_a = ea->total_alloc_bytes - ea->total_free_bytes;
    __u64 active_b = eb->total_alloc_bytes - eb->total_free_bytes;

    if (active_b > active_a)
        return 1;
    if (active_b < active_a)
        return -1;
    return 0;
}

int print_pid_stats_top(int pid_stats_fd, int top_n)
{
    struct pid_entry entries[MAX_DISPLAY_ENTRIES];
    int count = 0;
    __u32 key = 0, next_key;

    while (bpf_map_get_next_key(pid_stats_fd, &key, &next_key) == 0)
    {
        if (count >= MAX_DISPLAY_ENTRIES)
            break;

        struct pid_stats ps;
        if (bpf_map_lookup_elem(pid_stats_fd, &next_key, &ps) == 0)
        {
            entries[count].pid = next_key;
            entries[count].alloc_count = ps.alloc_count;
            entries[count].free_count = ps.free_count;
            entries[count].total_alloc_bytes = ps.total_alloc_bytes;
            entries[count].total_free_bytes = ps.total_free_bytes;
            memset(entries[count].comm, 0, TASK_COMM_LEN);
            count++;
        }
        key = next_key;
    }

    if (count == 0)
    {
        printf("  (no data yet)\n");
        return 0;
    }

    qsort(entries, count, sizeof(entries[0]), cmp_pid_entry);

    int display = (count < top_n) ? count : top_n;

    printf("  %-8s  %-10s  %-10s  %-12s  %-12s  %-12s\n",
           "PID", "ALLOCS", "FREES", "ALLOC_BYTES", "FREE_BYTES", "ACTIVE");

    for (int i = 0; i < display; i++)
    {
        char alloc_buf[16], free_buf[16], active_buf[16];
        __u64 active = entries[i].total_alloc_bytes - entries[i].total_free_bytes;

        print_size_human(entries[i].total_alloc_bytes, alloc_buf, sizeof(alloc_buf));
        print_size_human(entries[i].total_free_bytes, free_buf, sizeof(free_buf));
        print_size_human(active, active_buf, sizeof(active_buf));

        printf("  %-8u  %-10" PRIu64 "  %-10" PRIu64 "  %-12s  %-12s  %-12s\n",
               entries[i].pid,
               entries[i].alloc_count,
               entries[i].free_count,
               alloc_buf, free_buf, active_buf);
    }

    return count;
}

/* ─── Histogram ────────────────────────────────────────────────────────────── */

int print_histogram(int hist_fd)
{
    __u64 total = 0;
    __u64 values[NUM_HIST_BUCKETS] = {0};

    int num_cpus = libbpf_num_possible_cpus();
    if (num_cpus < 0)
        return -1;

    for (__u32 i = 0; i < NUM_HIST_BUCKETS; i++)
    {
        __u64 percpu_vals[num_cpus];
        memset(percpu_vals, 0, sizeof(percpu_vals));

        if (bpf_map_lookup_elem(hist_fd, &i, percpu_vals) == 0)
        {
            for (int c = 0; c < num_cpus; c++)
                values[i] += percpu_vals[c];
        }
        total += values[i];
    }

    if (total == 0)
    {
        printf("  (no allocations recorded)\n");
        return 0;
    }

    __u64 max_val = 0;
    for (int i = 0; i < NUM_HIST_BUCKETS; i++)
    {
        if (values[i] > max_val)
            max_val = values[i];
    }

    printf("  %-12s  %-10s  %s\n", "SIZE_RANGE", "COUNT", "DISTRIBUTION");

    for (int i = 0; i < NUM_HIST_BUCKETS; i++)
    {
        int bar_len = 0;
        if (max_val > 0)
            bar_len = (int)(values[i] * 40 / max_val);

        char bar[41];
        memset(bar, '#', bar_len);
        bar[bar_len] = '\0';

        printf("  %-12s  %-10" PRIu64 "  |%s\n",
               hist_bucket_str(i), values[i], bar);
    }

    printf("  Total allocations: %" PRIu64 "\n", total);
    return 0;
}

/* ─── Stack TopN ───────────────────────────────────────────────────────────── */

static int cmp_stack_entry(const void *a, const void *b)
{
    const struct stack_entry *ea = a;
    const struct stack_entry *eb = b;

    if (eb->total_bytes > ea->total_bytes)
        return 1;
    if (eb->total_bytes < ea->total_bytes)
        return -1;
    return 0;
}

int print_stack_top(int stack_fd, int alloc_fd, int top_n)
{
    struct stack_entry stacks[MAX_DISPLAY_ENTRIES];
    int stack_count = 0;

    __u64 key = 0, next_key;
    while (bpf_map_get_next_key(alloc_fd, &key, &next_key) == 0)
    {
        struct alloc_info info;
        if (bpf_map_lookup_elem(alloc_fd, &next_key, &info) != 0)
        {
            key = next_key;
            continue;
        }

        if (info.stack_id < 0)
        {
            key = next_key;
            continue;
        }

        bool found = false;
        for (int i = 0; i < stack_count; i++)
        {
            if (stacks[i].stack_id == info.stack_id)
            {
                stacks[i].total_bytes += info.size;
                stacks[i].count++;
                found = true;
                break;
            }
        }

        if (!found && stack_count < MAX_DISPLAY_ENTRIES)
        {
            stacks[stack_count].stack_id = info.stack_id;
            stacks[stack_count].total_bytes = info.size;
            stacks[stack_count].count = 1;
            stack_count++;
        }

        key = next_key;
    }

    if (stack_count == 0)
    {
        printf("  (no stack data)\n");
        return 0;
    }

    qsort(stacks, stack_count, sizeof(stacks[0]), cmp_stack_entry);

    int display = (stack_count < top_n) ? stack_count : top_n;

    for (int i = 0; i < display; i++)
    {
        char size_buf[16];
        print_size_human(stacks[i].total_bytes, size_buf, sizeof(size_buf));

        printf("  #%d  stack_id=%-6d  %s (%" PRIu64 " allocs)\n",
               i + 1, stacks[i].stack_id, size_buf, stacks[i].count);

        __u64 stack_addrs[MAX_STACK_DEPTH];
        memset(stack_addrs, 0, sizeof(stack_addrs));

        __u32 sid = (__u32)stacks[i].stack_id;
        if (bpf_map_lookup_elem(stack_fd, &sid, stack_addrs) == 0)
        {
            for (int j = 0; j < MAX_STACK_DEPTH && stack_addrs[j]; j++) {
                const char *sym = symbol_resolve(stack_addrs[j]);
                if (sym)
                    printf("       [%d] %s\n", j, sym);
                else
                    printf("       [%d] 0x%" PRIx64 "\n", j, stack_addrs[j]);
            }
        }
        printf("\n");
    }

    return 0;
}

/* ─── Drop stats ───────────────────────────────────────────────────────────── */

void print_drop_stats(int stats_fd)
{
    __u32 key;
    __u64 val;

    key = STATS_EVENTS_TOTAL;
    __u64 total_events = 0;
    if (bpf_map_lookup_elem(stats_fd, &key, &val) == 0)
        total_events = val;

    key = STATS_EVENTS_DROPPED;
    __u64 dropped = 0;
    if (bpf_map_lookup_elem(stats_fd, &key, &val) == 0)
        dropped = val;

    printf("  Events: total=%" PRIu64 "  dropped=%" PRIu64, total_events, dropped);

    if (total_events > 0)
    {
        double drop_rate = (double)dropped / (double)(total_events + dropped) * 100.0;
        printf("  drop_rate=%.2f%%", drop_rate);
    }
    printf("\n");
}

/* ─── Leak Detection ──────────────────────────────────────────────────────── */

static int cmp_leak_entry(const void *a, const void *b)
{
    const struct leak_entry *ea = a;
    const struct leak_entry *eb = b;

    if (eb->total_bytes > ea->total_bytes)
        return 1;
    if (eb->total_bytes < ea->total_bytes)
        return -1;
    return 0;
}

static void print_age_human(__u64 age_ns, char *buf, size_t buf_sz)
{
    __u64 age_sec = age_ns / 1000000000ULL;

    if (age_sec >= 3600)
        snprintf(buf, buf_sz, "%lluh%llum",
                 (unsigned long long)(age_sec / 3600),
                 (unsigned long long)((age_sec % 3600) / 60));
    else if (age_sec >= 60)
        snprintf(buf, buf_sz, "%llum%llus",
                 (unsigned long long)(age_sec / 60),
                 (unsigned long long)(age_sec % 60));
    else
        snprintf(buf, buf_sz, "%llus", (unsigned long long)age_sec);
}

int print_leak_suspects(int stack_fd, int alloc_fd,
                        __u64 min_age_ns, __u64 min_size, int top_n)
{
    struct leak_entry entries[MAX_DISPLAY_ENTRIES];
    int count = 0;

    __u64 now_ns = 0;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        now_ns = (__u64)ts.tv_sec * 1000000000ULL + (__u64)ts.tv_nsec;

    /* If CLOCK_MONOTONIC fails, use a rough estimate */
    if (now_ns == 0)
        now_ns = (__u64)time(NULL) * 1000000000ULL;

    __u64 key = 0, next_key;
    while (bpf_map_get_next_key(alloc_fd, &key, &next_key) == 0)
    {
        struct alloc_info info;
        if (bpf_map_lookup_elem(alloc_fd, &next_key, &info) != 0)
        {
            key = next_key;
            continue;
        }

        /* Check age threshold */
        if (now_ns > info.timestamp_ns) {
            __u64 age_ns = now_ns - info.timestamp_ns;
            if (age_ns < min_age_ns)
            {
                key = next_key;
                continue;
            }
        } else {
            /* timestamp is in the future? skip */
            key = next_key;
            continue;
        }

        /* Check size threshold */
        if (info.size < min_size)
        {
            key = next_key;
            continue;
        }

        __u64 age_ns = now_ns - info.timestamp_ns;

        /* Try to merge into existing entry with same stack_id */
        bool found = false;
        for (int i = 0; i < count; i++)
        {
            if (entries[i].stack_id == info.stack_id)
            {
                entries[i].total_bytes += info.size;
                entries[i].count++;
                if (age_ns > entries[i].oldest_age_ns)
                    entries[i].oldest_age_ns = age_ns;
                found = true;
                break;
            }
        }

        if (!found && count < MAX_DISPLAY_ENTRIES)
        {
            entries[count].stack_id = info.stack_id;
            entries[count].total_bytes = info.size;
            entries[count].count = 1;
            entries[count].oldest_age_ns = age_ns;
            count++;
        }

        key = next_key;
    }

    if (count == 0)
    {
        printf("  (no suspected leaks older than threshold)\n");
        return 0;
    }

    qsort(entries, count, sizeof(entries[0]), cmp_leak_entry);

    int display = (count < top_n) ? count : top_n;

    for (int i = 0; i < display; i++)
    {
        char size_buf[16], age_buf[32];
        print_size_human(entries[i].total_bytes, size_buf, sizeof(size_buf));
        print_age_human(entries[i].oldest_age_ns, age_buf, sizeof(age_buf));

        printf("  #%d  %s (%" PRIu64 " objs, oldest=%s)  stack_id=%d\n",
               i + 1, size_buf, entries[i].count,
               age_buf, entries[i].stack_id);

        /* Print stack trace with symbol resolution */
        __u64 stack_addrs[MAX_STACK_DEPTH];
        memset(stack_addrs, 0, sizeof(stack_addrs));

        __u32 sid = (__u32)entries[i].stack_id;
        if (entries[i].stack_id >= 0 &&
            bpf_map_lookup_elem(stack_fd, &sid, stack_addrs) == 0)
        {
            for (int j = 0; j < MAX_STACK_DEPTH && stack_addrs[j]; j++) {
                const char *sym = symbol_resolve(stack_addrs[j]);
                if (sym)
                    printf("       [%d] %s\n", j, sym);
                else
                    printf("       [%d] 0x%" PRIx64 "\n", j, stack_addrs[j]);
            }
        }
        printf("\n");
    }

    printf("  Total suspected leak groups: %d\n", count);
    return count;
}
