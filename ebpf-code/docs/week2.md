# Week 2-3 代码分析：数据流转与架构

## 一、项目当前文件结构

```text
ebpf-code/
├── Makefile                        # 构建系统
├── vmlinux.h                       # 内核 BTF 类型定义
├── include/
│   └── mem_common.h                # 内核态/用户态共享数据结构
├── bpf/
│   └── mem.bpf.c                   # eBPF 内核态程序
├── user/
│   ├── main.c                      # 用户态主程序（事件循环 + 报告）
│   ├── stats.c                     # 统计输出模块
│   └── include/
│       ├── mem_user.h              # CLI 解析 + 辅助函数
│       └── stats.h                 # stats 模块接口
├── scripts/
│   ├── run.sh                      # 运行脚本
│   ├── stress_alloc.sh             # 压力测试脚本
│   └── gen_vmlinux.sh              # vmlinux.h 生成
└── docs/
    ├── week1.md
    ├── week2.md                    # 本文件
    └── code-analysis.md
```

## 二、构建流程

```text
vmlinux.h ─────────────────────────────────────────────┐
                                                       │
include/mem_common.h ──┐                               │
                       │                               │
                       v                               v
              bpf/mem.bpf.c ──[clang -target bpf]──> output/mem.bpf.o
                                                       │
                                                       v
                                              [bpftool gen skeleton]
                                                       │
                                                       v
                                              user/mem.skel.h
                                                       │
              user/main.c ─────────────────────────────┤
              user/stats.c ────────────────────────────┤
              user/include/mem_user.h ─────────────────┤
              user/include/stats.h ────────────────────┤
                                                       │
                                                       v
                                            [gcc -lbpf -lelf -lz]
                                                       │
                                                       v
                                            output/mem-analyzer
```

关键点：
- `mem_common.h` 被内核态和用户态同时 include，通过 `#ifdef __BPF__` 区分编译环境。
- skeleton 头文件 `mem.skel.h` 由 bpftool 从编译后的 BPF 对象文件自动生成，包含 map fd 访问接口和 attach/detach 函数。

## 三、BPF Maps 总览

项目定义了 7 个 BPF map，每个承担不同的数据职责：

| Map 名称 | 类型 | Key | Value | 容量 | 用途 |
|----------|------|-----|-------|------|------|
| `events` | RINGBUF | — | `struct mem_event` | 4MB | 事件传输通道 |
| `stack_traces` | STACK_TRACE | `__u32` (stack_id) | `__u64[32]` (地址数组) | 16384 | 内核调用栈存储 |
| `pid_stats_map` | HASH | `__u32` (tgid) | `struct pid_stats` | 10240 | 每进程分配/释放统计 |
| `alloc_table` | HASH | `__u64` (ptr) | `struct alloc_info` | 65536 | 活跃分配追踪 |
| `hist_map` | PERCPU_ARRAY | `__u32` (bucket) | `__u64` (count) | 8 | 分配大小分布 |
| `target_pid` | ARRAY | `__u32` (0) | `__u32` (pid) | 1 | PID 过滤配置 |
| `stats` | ARRAY | `__u32` (idx) | `__u64` (count) | 2 | 事件总数/丢弃计数 |

## 四、完整数据流转图

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                         KERNEL SPACE                                     │
│                                                                         │
│  ┌─────────────────┐     ┌─────────────────┐                           │
│  │ kmalloc()       │     │ kfree()         │                           │
│  │ (内核内存分配)   │     │ (内核内存释放)   │                           │
│  └────────┬────────┘     └────────┬────────┘                           │
│           │                       │                                     │
│           v                       v                                     │
│  tracepoint/kmem/kmalloc  tracepoint/kmem/kfree                        │
│           │                       │                                     │
│           v                       v                                     │
│  ┌────────────────────────────────────────────────────────────┐        │
│  │              eBPF Program (mem.bpf.c)                       │        │
│  │                                                            │        │
│  │  1. bpf_get_current_pid_tgid() → pid, tgid                │        │
│  │  2. should_filter(tgid) → 检查 target_pid map             │        │
│  │  3. [ALLOC 路径]                                           │        │
│  │     a. pid_stats_map[tgid].alloc_count++                   │        │
│  │     b. pid_stats_map[tgid].total_alloc_bytes += size       │        │
│  │     c. hist_map[size_to_bucket(size)]++  (percpu)          │        │
│  │     d. bpf_get_stackid() → stack_id                       │        │
│  │     e. alloc_table[ptr] = {size, ts, pid, stack_id}        │        │
│  │     f. bpf_ringbuf_reserve() → submit mem_event            │        │
│  │  4. [FREE 路径]                                            │        │
│  │     a. alloc_table[ptr] → 查出 freed_size                  │        │
│  │     b. pid_stats_map[tgid].free_count++                    │        │
│  │     c. pid_stats_map[tgid].total_free_bytes += freed_size  │        │
│  │     d. bpf_map_delete_elem(alloc_table, ptr)               │        │
│  │     e. bpf_ringbuf_reserve() → submit mem_event            │        │
│  │  5. stats[TOTAL]++ 或 stats[DROPPED]++                     │        │
│  └────────────────────────────────────────────────────────────┘        │
│           │              │            │           │          │          │
│           v              v            v           v          v          │
│     ┌──────────┐  ┌───────────┐ ┌─────────┐ ┌────────┐ ┌───────┐     │
│     │  events  │  │pid_stats  │ │hist_map │ │alloc   │ │stack  │     │
│     │ (ringbuf)│  │  _map     │ │(percpu) │ │_table  │ │_traces│     │
│     └────┬─────┘  └─────┬─────┘ └────┬────┘ └───┬────┘ └───┬───┘     │
│          │               │            │          │          │          │
└──────────┼───────────────┼────────────┼──────────┼──────────┼──────────┘
           │               │            │          │          │
    ring_buffer__poll      │   bpf_map_lookup     │   bpf_map_lookup
           │               │            │          │          │
           v               v            v          v          v
┌──────────────────────────────────────────────────────────────────────────┐
│                         USER SPACE                                        │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                    main.c (Coordinator)                           │   │
│  │                                                                  │   │
│  │  1. parse_opts() → 解析 CLI 参数                                  │   │
│  │  2. mem_bpf__open() → 打开 skeleton                              │   │
│  │  3. mem_bpf__load() → 加载 BPF 程序到内核                         │   │
│  │  4. 写入 target_pid map（如果指定了 --pid）                        │   │
│  │  5. mem_bpf__attach() → 挂载到 tracepoint                        │   │
│  │  6. ring_buffer__new() → 注册回调 handle_event                    │   │
│  │  7. 主循环:                                                       │   │
│  │     ┌─────────────────────────────────────────────────────┐      │   │
│  │     │  while (!exiting) {                                  │      │   │
│  │     │      ring_buffer__poll(rb, 100ms)                    │      │   │
│  │     │          → handle_event() [verbose 模式打印每条事件]  │      │   │
│  │     │      if (now - last_report >= interval)              │      │   │
│  │     │          → print_report()                            │      │   │
│  │     │      if (duration 到期)                               │      │   │
│  │     │          → break                                     │      │   │
│  │     │  }                                                   │      │   │
│  │     └─────────────────────────────────────────────────────┘      │   │
│  │  8. Final report + cleanup                                        │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                          │                                               │
│                          v                                               │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                    stats.c (分析输出)                              │   │
│  │                                                                  │   │
│  │  print_pid_stats_top():                                          │   │
│  │    - 遍历 pid_stats_map (bpf_map_get_next_key)                   │   │
│  │    - 按 active_bytes (alloc - free) 降序排序                      │   │
│  │    - 输出 TopN 进程                                               │   │
│  │                                                                  │   │
│  │  print_histogram():                                              │   │
│  │    - 读取 hist_map 的 8 个 percpu bucket                         │   │
│  │    - 汇总所有 CPU 的值                                            │   │
│  │    - 输出 ASCII bar chart                                        │   │
│  │                                                                  │   │
│  │  print_stack_top():                                              │   │
│  │    - 遍历 alloc_table 中所有活跃分配                              │   │
│  │    - 按 stack_id 聚合 total_bytes 和 count                       │   │
│  │    - 排序输出 TopN stack                                          │   │
│  │    - 从 stack_traces map 读取地址数组                             │   │
│  │                                                                  │   │
│  │  print_drop_stats():                                             │   │
│  │    - 读取 stats map 的 total 和 dropped                          │   │
│  │    - 计算丢事件率                                                 │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                          │                                               │
│                          v                                               │
│                    ┌───────────┐                                         │
│                    │  stdout   │                                         │
│                    │ (报告输出) │                                         │
│                    └───────────┘                                         │
└──────────────────────────────────────────────────────────────────────────┘
```

## 五、关键数据结构关系

```text
struct mem_event (56 bytes)          struct alloc_info (24 bytes)
┌──────────────────────┐            ┌──────────────────────┐
│ pid        (__u32)   │            │ size       (__u64)   │
│ tgid       (__u32)   │            │ timestamp  (__u64)   │
│ comm       (char[16])│            │ pid        (__u32)   │
│ size       (__u64)   │            │ stack_id   (__s32)   │
│ ptr        (__u64)   │            └──────────────────────┘
│ timestamp  (__u64)   │                     ^
│ stack_id   (__s32)   │                     │
│ type       (__u32)   │            alloc_table: ptr → alloc_info
└──────────────────────┘            (用于 kfree 时反查 size，
         │                           以及 stack 热点聚合)
         │
    通过 ring buffer
    传输到用户态

struct pid_stats (32 bytes)
┌──────────────────────────┐
│ alloc_count      (__u64) │
│ free_count       (__u64) │
│ total_alloc_bytes(__u64) │
│ total_free_bytes (__u64) │
└──────────────────────────┘
         ^
         │
pid_stats_map: tgid → pid_stats
(内核态原子更新，用户态周期读取)
```

## 六、ALLOC 事件完整路径（逐步）

```text
1. 内核某处调用 kmalloc(size, flags)
2. 触发 tracepoint/kmem/kmalloc
3. eBPF 程序 tracepoint_kmalloc() 被调用
4. 从 ctx 中读取 ptr 和 bytes_alloc
5. 调用 trace_alloc(ctx, ptr, bytes_alloc):
   a. 检查 bytes_alloc == 0 或 ptr == NULL → 跳过
   b. bpf_get_current_pid_tgid() → 拆分出 pid 和 tgid
   c. should_filter(tgid) → 查 target_pid map，不匹配则跳过
   d. 查 pid_stats_map[tgid]:
      - 存在 → 原子递增 alloc_count 和 total_alloc_bytes
      - 不存在 → 插入新条目 (BPF_NOEXIST)
   e. size_to_bucket(bytes_alloc) → 得到 histogram bucket index
   f. hist_map[bucket]++ (percpu，无锁竞争)
   g. bpf_get_stackid(ctx, &stack_traces, flags) → 获取调用栈 ID
   h. 构造 alloc_info，写入 alloc_table[ptr]
   i. bpf_ringbuf_reserve(&events, sizeof(mem_event), 0):
      - 成功 → 填充字段，bpf_ringbuf_submit()，stats[TOTAL]++
      - 失败 → stats[DROPPED]++，返回
```

## 七、FREE 事件完整路径（逐步）

```text
1. 内核某处调用 kfree(ptr)
2. 触发 tracepoint/kmem/kfree
3. eBPF 程序 tracepoint_kfree() 被调用
4. 从 ctx 中读取 ptr
5. 调用 trace_free(ctx, ptr):
   a. 检查 ptr == NULL → 跳过
   b. bpf_get_current_pid_tgid() → pid, tgid
   c. should_filter(tgid) → 过滤检查
   d. bpf_map_lookup_elem(alloc_table, &ptr):
      - 找到 → freed_size = info->size
      - 未找到 → freed_size = 0（可能是过滤前的分配或 map 满时未记录）
   e. 查 pid_stats_map[tgid]:
      - 存在 → 原子递增 free_count 和 total_free_bytes
   f. bpf_map_delete_elem(alloc_table, &ptr) → 移除追踪记录
   g. bpf_ringbuf_reserve() → 提交 FREE 事件到 ring buffer
```

## 八、用户态消费与报告流程

```text
main loop (100ms poll 超时):
│
├── ring_buffer__poll(rb, 100)
│   │
│   └── 对每个事件调用 handle_event():
│       └── verbose 模式: 打印单条事件详情
│           非 verbose: 直接返回（事件仍被消费，只是不打印）
│
├── 检查是否到达 interval:
│   │
│   └── print_report(skel):
│       ├── print_pid_stats_top()
│       │   ├── bpf_map_get_next_key() 遍历 pid_stats_map
│       │   ├── 收集所有条目到本地数组
│       │   ├── qsort() 按 active_bytes 降序
│       │   └── 输出 TopN
│       │
│       ├── [--hist] print_histogram()
│       │   ├── 读取 8 个 percpu bucket
│       │   ├── 汇总各 CPU 值
│       │   └── 输出 ASCII 柱状图
│       │
│       ├── [--stack] print_stack_top()
│       │   ├── 遍历 alloc_table 所有活跃分配
│       │   ├── 按 stack_id 聚合
│       │   ├── qsort() 按 total_bytes 降序
│       │   ├── 输出 TopN stack
│       │   └── 从 stack_traces map 读取地址
│       │
│       └── print_drop_stats()
│           └── 读取 stats map，计算丢事件率
│
└── 检查 duration 是否到期 → 退出
```

## 九、并发与一致性分析

### 内核态并发

| 操作 | 并发场景 | 保护机制 |
|------|----------|----------|
| pid_stats_map 更新 | 多 CPU 同时 kmalloc | `__sync_fetch_and_add` 原子操作 |
| alloc_table 插入/删除 | 同一 ptr 的 alloc 和 free | BPF hash map 内部自旋锁 |
| hist_map 更新 | 多 CPU 同时分配 | PERCPU_ARRAY，每 CPU 独立计数，无竞争 |
| ring buffer 写入 | 多 CPU 同时提交事件 | ring buffer 内部 lock-free 机制 |
| stats 更新 | 多 CPU 同时计数 | `__sync_fetch_and_add` 原子操作 |

### 用户态读取

- 用户态通过 `bpf_map_lookup_elem` / `bpf_map_get_next_key` 读取 map。
- 读取不加锁，看到的是某个时刻的快照，不保证全局一致性。
- 对于统计展示场景，这种弱一致性是可接受的。

### 潜在问题

1. **pid_stats_map 的 BPF_NOEXIST 竞争**：两个 CPU 同时为同一个新 tgid 插入，一个会失败。失败的那次分配不会被统计。实际影响很小（只丢第一次）。

2. **alloc_table 容量上限**：max_entries=65536。如果活跃分配超过这个数，新分配无法记录，后续 kfree 时查不到 size，freed_size 为 0。影响 pid_stats 的 total_free_bytes 准确性。

3. **stack_traces map 容量**：max_entries=16384。不同调用栈超过这个数时，`bpf_get_stackid` 返回负值，stack_id 为 -1，该分配不参与 stack 热点统计。

## 十、设计取舍总结

| 决策 | 选择 | 原因 |
|------|------|------|
| 事件传输 | ring buffer (非 perf buffer) | 全局共享、API 简洁、适合高频事件 |
| PID 统计位置 | 内核态 map | 避免每个事件都传到用户态再聚合，降低 ring buffer 压力 |
| Histogram 类型 | PERCPU_ARRAY | 高频更新无锁竞争，用户态读取时汇总 |
| alloc_table 类型 | HASH (非 PERCPU) | ptr 是全局唯一的，不能按 CPU 分 |
| PID 过滤位置 | 内核态 | 在源头过滤，减少无关事件的 map 操作和 ring buffer 占用 |
| Stack trace | BPF_F_FAST_STACK_CMP + REUSE | 减少 map 空间占用，相同栈复用同一 ID |
| 统计原子性 | `__sync_fetch_and_add` | 比 BPF spin lock 开销低，适合简单计数场景 |
| 用户态报告 | 周期轮询 + 定时输出 | 简单可靠，避免多线程复杂度 |
