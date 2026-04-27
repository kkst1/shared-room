# ebpf-code 项目详细报告

## 1. 项目概述

`ebpf-code` 是一个基于 eBPF/libbpf/CO-RE 的 Linux 内核内存事件采集工具，项目名称可以定位为 `mem-analyzer`。当前代码已经完成了 eBPF 项目的最小可运行闭环：在内核侧挂载 `kmalloc` 和 `kfree` tracepoint，采集内核内存分配与释放事件，通过 BPF ring buffer 将事件发送到用户态，用户态程序使用 libbpf skeleton 加载、attach 并实时打印事件。

当前项目处于“第 1 阶段到第 2 阶段早期”的状态，已经具备如下能力：

- 使用 libbpf skeleton 加载 eBPF 程序。
- 使用 CO-RE 依赖的 `vmlinux.h` 描述内核类型。
- 使用 tracepoint 采集 `kmalloc` / `kfree` 事件。
- 使用 BPF ring buffer 进行内核态到用户态的数据传输。
- 用户态程序能够接收事件并格式化输出。
- 提供构建脚本、运行脚本和简单压力脚本。

项目还未实现 PID 统计、调用栈采集、histogram、符号解析和疑似泄漏检测。因此它目前更适合作为 eBPF 内存分析项目的基础骨架，而不是完整分析工具。

## 2. 目录结构分析

当前目录结构如下：

```text
ebpf-code/
├── README.md
├── Makefile
├── bpf/
│   └── mem.bpf.c
├── include/
│   └── mem_common.h
├── user/
│   ├── main.c
│   └── include/
│       └── mem_user.h
└── scripts/
    ├── gen_vmlinux.sh
    ├── run.sh
    └── stress_alloc.sh
```

各模块职责：

- `README.md`：说明项目定位、环境依赖、构建方法、运行方法和示例输出。
- `Makefile`：负责编译 BPF object、生成 skeleton、编译用户态程序。
- `bpf/mem.bpf.c`：内核态 eBPF 程序，负责采集内存分配/释放事件。
- `include/mem_common.h`：内核态和用户态共享的数据结构定义。
- `user/main.c`：用户态加载器和事件消费程序。
- `user/include/mem_user.h`：用户态辅助函数，目前主要负责事件类型转字符串。
- `scripts/gen_vmlinux.sh`：从当前运行内核的 BTF 生成 `vmlinux.h`。
- `scripts/run.sh`：检查二进制并用 root 权限运行程序。
- `scripts/stress_alloc.sh`：通过文件写入制造一定内核内存分配压力。

## 3. 当前系统架构

当前系统可以拆成三层：

```text
                    +--------------------------+
                    |       user/main.c        |
                    |--------------------------|
                    | load skeleton            |
                    | attach tracepoints       |
                    | poll ring buffer         |
                    | print mem_event          |
                    +------------+-------------+
                                 ^
                                 |
                         BPF ring buffer
                                 |
                                 v
                    +--------------------------+
                    |      bpf/mem.bpf.c       |
                    |--------------------------|
                    | tracepoint/kmem/kmalloc  |
                    | tracepoint/kmem/kfree    |
                    | build mem_event          |
                    | submit to ringbuf        |
                    +------------+-------------+
                                 ^
                                 |
                    +------------+-------------+
                    | Linux kernel kmem events |
                    +--------------------------+
```

数据流：

1. Linux 内核触发 `kmem/kmalloc` 或 `kmem/kfree` tracepoint。
2. eBPF 程序进入对应处理函数。
3. eBPF 程序从 tracepoint context 中读取指针和分配大小。
4. eBPF 程序调用 helper 获取进程信息、时间戳和进程名。
5. eBPF 程序构造 `struct mem_event`。
6. 事件通过 ring buffer 发送到用户态。
7. 用户态 `ring_buffer__poll` 轮询事件。
8. 回调函数 `handle_event` 格式化打印事件。

## 4. 共享数据结构

共享头文件位于 `include/mem_common.h`，它是内核态和用户态之间的数据协议。

核心定义：

```c
#define TASK_COMM_LEN 16
#define MAX_ENTRIES 10240

enum event_type {
    EVENT_ALLOC = 1,
    EVENT_FREE  = 2,
};

struct mem_event {
    __u32 pid;
    __u32 tgid;
    char  comm[TASK_COMM_LEN];
    __u64 size;
    __u64 ptr;
    __u64 timestamp_ns;
    __s32 stack_id;
    __u32 type;
};
```

字段含义：

- `pid`：当前实现中写入的是 `pid_tgid >> 32`，在 Linux 语义中更接近 TGID，也就是进程 ID。
- `tgid`：当前实现中写入的是 `pid_tgid` 的低 32 位，实际更接近线程 ID。字段命名和赋值语义后续建议修正。
- `comm`：当前任务名，长度 16。
- `size`：分配大小。释放事件中设置为 0。
- `ptr`：分配或释放的内核地址。
- `timestamp_ns`：事件发生时的内核单调时间，单位纳秒。
- `stack_id`：当前保留字段，固定为 -1，还没有接入 stack trace。
- `type`：事件类型，`EVENT_ALLOC` 或 `EVENT_FREE`。

需要注意：`MAX_ENTRIES` 当前没有被使用，可以为后续 hash map、stack map 或统计 map 预留。

## 5. eBPF 内核态程序分析

核心文件：`bpf/mem.bpf.c`

### 5.1 依赖与 CO-RE 基础

文件开头引入：

```c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "../include/mem_common.h"
```

说明：

- `vmlinux.h` 提供当前内核 BTF 类型定义，是 CO-RE 项目的基础。
- `bpf_helpers.h` 提供 eBPF helper 声明。
- `bpf_tracing.h` 提供 tracepoint/kprobe 相关宏和辅助定义。
- `bpf_core_read.h` 支持 CO-RE 读取，但当前代码还没有显式使用 `BPF_CORE_READ`。
- `mem_common.h` 保证用户态和内核态使用同一份事件结构。

### 5.2 ring buffer map

当前定义：

```c
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");
```

含义：

- map 类型是 `BPF_MAP_TYPE_RINGBUF`。
- ring buffer 大小是 256 KB。
- 所有内存事件通过这个 map 上报用户态。

优点：

- API 简洁，适合事件流。
- 对高频事件比普通 printk 更合适。
- 用户态可以通过 libbpf ring buffer API 消费。

局限：

- ring buffer 满时 `bpf_ringbuf_reserve` 会失败，当前代码直接丢弃事件，没有计数。
- 256 KB 对高频 `kmalloc/kfree` 场景可能偏小，后续需要压测后调整。

### 5.3 分配事件采集

`trace_alloc` 是分配事件的通用处理函数：

```c
static __always_inline int trace_alloc(void *ctx, unsigned long ptr,
                                       size_t bytes_alloc)
```

逻辑：

1. 如果 `bytes_alloc == 0`，直接返回。
2. 从 ring buffer 申请一块 `mem_event` 空间。
3. 获取当前 `pid_tgid`。
4. 填充 pid、tgid、size、ptr、type、timestamp、stack_id、comm。
5. 提交事件到 ring buffer。

挂载点：

```c
SEC("tracepoint/kmem/kmalloc")
int tracepoint_kmalloc(struct trace_event_raw_kmalloc *ctx)
{
    return trace_alloc(ctx, ctx->ptr, ctx->bytes_alloc);
}
```

这说明当前项目使用的是 tracepoint，而不是 kprobe。tracepoint 的好处是接口相对稳定，更适合学习项目和可移植演示。

### 5.4 释放事件采集

`trace_free` 是释放事件的通用处理函数：

```c
static __always_inline int trace_free(void *ctx, unsigned long ptr)
```

逻辑：

1. 如果 `ptr` 为空，直接返回。
2. 从 ring buffer 申请事件空间。
3. 获取进程信息。
4. 设置 `size = 0`。
5. 设置事件类型为 `EVENT_FREE`。
6. 提交事件。

挂载点：

```c
SEC("tracepoint/kmem/kfree")
int tracepoint_kfree(struct trace_event_raw_kfree *ctx)
{
    return trace_free(ctx, ctx->ptr);
}
```

当前释放事件只知道释放指针，不知道释放大小。后续如果要计算 active bytes 或做泄漏检测，需要在 alloc 时记录 `ptr -> alloc_info`，free 时通过 ptr 查表获取大小并删除记录。

### 5.5 当前内核态能力总结

已经实现：

- 采集 kmalloc。
- 采集 kfree。
- 获取当前进程名。
- 获取当前进程/线程 ID。
- 获取纳秒级时间戳。
- 使用 ring buffer 上报。

尚未实现：

- 丢事件统计。
- PID 过滤。
- 分配大小过滤。
- stack trace 采集。
- `ptr -> alloc_info` 生命周期跟踪。
- kmem_cache_alloc/free 采集。
- page alloc/free 采集。
- map 侧聚合统计。

## 6. 用户态程序分析

核心文件：`user/main.c`

### 6.1 程序启动流程

用户态主流程：

```text
注册 SIGINT/SIGTERM 信号处理
open_and_load BPF skeleton
attach BPF programs
创建 ring_buffer
打印表头
循环 poll ring buffer
退出时释放资源
```

关键代码：

```c
skel = mem_bpf__open_and_load();
err = mem_bpf__attach(skel);
rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
```

这说明项目已经使用了标准 libbpf skeleton 工作流：

1. BPF object 编译成 `.o`。
2. `bpftool gen skeleton` 生成 `user/mem.skel.h`。
3. 用户态 include skeleton。
4. 用户态调用 skeleton API 加载和 attach。

### 6.2 ring buffer 消费

事件回调：

```c
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
```

当前用户态只做了最小展示，没有进行统计聚合。它适合验证 eBPF 采集链路是否通畅，但还没有体现“分析工具”的价值。

### 6.3 信号与资源释放

程序使用：

```c
static volatile sig_atomic_t exiting = 0;
```

收到 SIGINT 或 SIGTERM 后退出轮询循环，并释放：

- `ring_buffer__free(rb)`
- `mem_bpf__destroy(skel)`

这是比较规范的最小用户态加载器写法。

### 6.4 用户态辅助头文件

`user/include/mem_user.h` 提供：

```c
static const char *event_type_str(__u32 type)
```

用于将 `EVENT_ALLOC` 和 `EVENT_FREE` 转换为字符串。该文件很小，但已经体现出将用户态展示逻辑从 main 中拆出去的意图。

## 7. 构建系统分析

核心文件：`Makefile`

构建链路：

```text
bpf/mem.bpf.c
    |
    | clang -target bpf
    v
output/mem.bpf.o
    |
    | bpftool gen skeleton
    v
user/mem.skel.h
    |
    | gcc + libbpf
    v
output/mem-analyzer
```

关键变量：

- `CLANG ?= clang`
- `BPFTOOL ?= bpftool`
- `CC ?= gcc`
- `BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -I./include -I/usr/include/bpf`
- `USER_LDFLAGS := -lbpf -lelf -lz`

优点：

- 构建链路清楚。
- 自动生成 skeleton。
- 用户态和 BPF 编译产物分离到 `output/`。

需要改进：

- `vmlinux.h` 是构建依赖，但 `make` 不会自动生成它，只要求它存在。虽然有 `vmlinux` target 和 `scripts/gen_vmlinux.sh`，但默认 `make` 前仍需要手动生成。
- `clean` 会删除输出目录和 skeleton，但不会删除 `vmlinux.h`，这通常是合理的。
- 用户态只编译 `user/main.c`，后续拆出 `loader.c/analyzer.c/symbol.c` 后需要扩展 Makefile。

## 8. 脚本分析

### 8.1 `scripts/gen_vmlinux.sh`

作用：从 `/sys/kernel/btf/vmlinux` 生成 `vmlinux.h`。

优点：

- 会检查 BTF 文件是否存在。
- 出错提示明确。
- 生成后打印行数。

### 8.2 `scripts/run.sh`

作用：运行 `output/mem-analyzer`。

逻辑：

- 检查二进制是否存在。
- 如果不是 root，则通过 sudo 重新执行。
- 将参数透传给程序。

这是很实用的演示脚本。

### 8.3 `scripts/stress_alloc.sh`

作用：通过创建大量临时文件制造内核内存分配压力。

当前逻辑：

```bash
for i in $(seq 1 1000); do
    dd if=/dev/zero of=/tmp/stress_alloc_$i bs=4096 count=10 2>/dev/null
done
rm -f /tmp/stress_alloc_*
```

这个脚本能触发一定文件系统和页缓存相关路径，但它不是专门针对 `kmalloc/kfree` 的精准测试程序。后续可以增加一个 C 压测程序，通过系统调用、socket、文件操作等方式制造更可控的内核分配场景。

## 9. 当前项目优点

### 9.1 技术路线正确

项目使用的是 libbpf + skeleton + CO-RE 的现代 eBPF 开发方式，不是老式 BCC 脚本，也不是简单 `bpf_trace_printk` demo。这对简历和面试更有说服力。

### 9.2 使用 tracepoint 更稳

当前挂载 `tracepoint/kmem/kmalloc` 和 `tracepoint/kmem/kfree`，相比直接 kprobe 内核函数，tracepoint ABI 更稳定，适合跨内核演示。

### 9.3 数据协议清楚

`mem_common.h` 将事件结构放在共享头文件中，用户态和内核态复用同一份定义，避免结构不一致。

### 9.4 最小闭环完整

从采集、传输、加载、attach、poll、打印到退出清理，整个链路是完整的。后续功能都可以在这个基础上逐步扩展。

## 10. 当前主要问题与风险

### 10.1 `pid` 和 `tgid` 字段语义可能写反

`bpf_get_current_pid_tgid()` 返回值通常是：

```text
高 32 位：tgid
低 32 位：pid
```

当前代码：

```c
e->pid  = pid_tgid >> 32;
e->tgid = pid_tgid;
```

这会导致：

- `e->pid` 实际存的是 tgid。
- `e->tgid` 因为截断为 `__u32`，实际存的是低 32 位 pid。

建议改为：

```c
e->tgid = pid_tgid >> 32;
e->pid = (__u32)pid_tgid;
```

或者如果 CLI 想展示进程 ID，可以把字段命名为 `tgid` 或输出时明确说明。

### 10.2 当前只打印事件，缺少分析能力

项目 README 中定位是“内存行为分析工具”，但当前只做事件打印。要成为完整项目，需要至少增加：

- PID 维度聚合。
- 分配大小 histogram。
- stack trace 热点分析。
- `ptr -> alloc_info` 生命周期跟踪。
- 疑似泄漏检测。

### 10.3 没有丢事件统计

当前：

```c
e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
if (!e)
    return 0;
```

如果 ring buffer 满了，事件会直接丢失。建议增加一个 per-cpu array 或普通 array map 统计 dropped events，这样压测时可以评估工具可靠性。

### 10.4 `stack_id` 字段还没有使用

`mem_event` 中已经包含 `stack_id`，但当前固定为 -1。后续应添加：

```c
BPF_MAP_TYPE_STACK_TRACE
bpf_get_stackid(ctx, &stack_traces, 0)
```

这样项目才能定位“哪个调用路径在分配内存”，这是面试中最重要的亮点之一。

### 10.5 释放事件没有 size

`kfree` 事件里只有指针，当前 size 设置为 0。要计算 active bytes，需要 alloc 时记录指针对应的 size，free 时查表补齐。

建议增加：

```text
allocs: hash map
key: ptr
value: size, timestamp, pid, stack_id
```

### 10.6 采集点覆盖不足

当前只覆盖：

- `kmalloc`
- `kfree`

后续应逐步扩展：

- `kmem_cache_alloc`
- `kmem_cache_free`
- `mm_page_alloc`
- `mm_page_free`

这样才能更完整地覆盖 slab/page 两类路径。

## 11. 建议的下一阶段实现路线

### 阶段 1：修正基础语义

建议优先做：

- 修正 `pid/tgid` 字段赋值。
- 给 ring buffer reserve 失败增加 dropped counter。
- 给用户态增加 `--duration`，方便自动测试。
- README 增加当前限制说明。

### 阶段 2：增加 PID 统计

用户态维护：

```text
pid -> {
    comm,
    alloc_count,
    free_count,
    total_alloc,
    active_bytes
}
```

输出 TopN：

```text
PID     COMM        ALLOC    FREE     ACTIVE_BYTES
1234    bash        1024     1001     64KB
```

### 阶段 3：增加 alloc table

内核态或用户态记录：

```text
ptr -> alloc_info
```

推荐早期先放用户态实现，降低 verifier 和 map 复杂度。事件流中 alloc 插入，free 删除，即可得到 active bytes 和疑似泄漏基础。

### 阶段 4：增加 histogram

按分配大小做 bucket：

```text
0-64B
64-256B
256B-1KB
1KB-4KB
4KB-16KB
16KB+
```

可以先在用户态实现，后续再考虑 eBPF map 聚合。

### 阶段 5：增加 stack trace

内核态增加：

```text
stack_traces: BPF_MAP_TYPE_STACK_TRACE
```

分配时：

```text
stack_id = bpf_get_stackid(ctx, &stack_traces, 0)
```

用户态按 stack id 聚合：

```text
stack_id -> count, bytes
```

### 阶段 6：增加符号解析

用户态读取：

- `/proc/kallsyms`
- 或结合 BTF/libbpf 能力

将地址转为函数名，输出 TopN 调用栈。

### 阶段 7：疑似泄漏检测

基于 alloc table 扫描：

```text
now - alloc.timestamp > leak_after
```

输出：

```text
PID     COMM      PTR             SIZE     AGE     STACK
1234    nginx     0xffff...       4096     15s     kmem_cache_alloc -> ...
```

## 12. 推荐最终 CLI 形态

建议将当前单一打印模式扩展为：

```text
mem-analyzer [options]

Options:
  --pid <pid>             只分析指定 PID/TGID
  --top <n>               输出 TopN
  --interval <sec>        输出刷新间隔
  --duration <sec>        运行指定时间后退出
  --hist                  显示分配大小分布
  --stack                 显示调用栈热点
  --leak                  开启疑似泄漏检测
  --leak-after <sec>      超过指定时间未释放则标记为疑似泄漏
  --min-size <bytes>      忽略小分配
  --raw                   保留当前逐事件打印模式
```

当前的逐事件打印模式不建议删除，可以保留为 `--raw`，用于调试和教学演示。

## 13. 简历表达建议

当前版本可以这样写：

> 基于 eBPF/libbpf/CO-RE 实现 Linux 内核内存事件采集工具，使用 tracepoint 追踪 `kmalloc/kfree`，通过 BPF ring buffer 将内核内存分配/释放事件实时上报用户态，并基于 libbpf skeleton 完成 eBPF 程序加载、挂载和事件消费链路。

等后续实现统计、stack trace 和泄漏检测后，可以升级为：

> 基于 eBPF/libbpf/CO-RE 实现 Linux 内核内存行为分析工具，采集 `kmalloc/kfree/kmem_cache_alloc` 等内存事件，通过 ring buffer 上报用户态，结合 PID 聚合、log2 histogram、kernel stack trace、符号解析和 ptr 生命周期跟踪，实现内存分配热点定位与疑似泄漏检测。

## 14. 面试讲解版本

可以这样介绍当前项目：

> 这个项目是一个基于 eBPF 的 Linux 内核内存事件采集工具。我在内核态使用 tracepoint 挂载 `kmalloc` 和 `kfree`，采集分配大小、释放指针、进程信息和时间戳，然后通过 BPF ring buffer 发送到用户态。用户态使用 libbpf skeleton 完成 BPF 程序加载、attach 和事件轮询，并实时打印事件。当前版本完成了 eBPF 内存分析工具的最小闭环，后续会在用户态增加 PID 聚合、histogram、stack trace 热点分析和疑似泄漏检测。

如果被问为什么用 tracepoint：

> 因为 tracepoint 相比 kprobe 更稳定，字段来自内核定义的 trace event，上手和跨版本演示都更稳。后续如果要覆盖 tracepoint 没有的路径，可以再补 kprobe 或 fentry。

如果被问为什么用 ring buffer：

> 内存分配事件频率高，不能用 printk 这类重方式。ring buffer 适合事件流传输，用户态消费简单，开销也更可控。

如果被问为什么现在不在 eBPF 里做复杂分析：

> eBPF 受 verifier 限制，复杂排序、符号解析、长时间窗口分析都更适合放在用户态。内核态保持轻量，只采集必要信息和做少量过滤，整体更稳定。

## 15. 总结

`ebpf-code` 当前是一个方向正确、链路完整的 eBPF 内存分析项目雏形。它已经具备现代 eBPF 项目的关键基础：CO-RE、skeleton、tracepoint、ring buffer 和用户态消费程序。当前最大的问题是功能还停留在事件打印层面，距离“内存行为分析与异常检测系统”还缺少统计聚合、调用栈、符号解析和泄漏检测。

建议下一步先修正 `pid/tgid` 字段语义，然后增加用户态 PID 统计和 alloc table。这样项目会从“能跑的 demo”快速升级为“能分析的工具”。在此基础上接入 stack trace 和符号解析，就能形成面试和简历中最有说服力的核心亮点。
