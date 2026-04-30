# eBPF 内存分析器 — 代码逐模块解析

本文档对 `ebpf-code` 项目的每个源文件进行逐段分析，目标是让读者理解每一行代码在做什么、为什么这么做。

---

## 目录

1. [项目全景](#1-项目全景)
2. [构建流程 — Makefile](#2-构建流程--makefile)
3. [共享数据协议 — mem_common.h](#3-共享数据协议--mem_commonh)
4. [内核态 eBPF 程序 — mem.bpf.c](#4-内核态-ebpf-程序--membpfc)
5. [用户态加载器 — main.c](#5-用户态加载器--mainc)
6. [用户态辅助 — mem_user.h](#6-用户态辅助--mem_userh)
7. [辅助脚本](#7-辅助脚本)
8. [数据流全链路图](#8-数据流全链路图)
9. [已知问题与改进方向](#9-已知问题与改进方向)

---

## 1. 项目全景

```
ebpf-code/
├── Makefile                  # 构建系统
├── bpf/mem.bpf.c             # 内核态 eBPF 程序（数据采集）
├── include/mem_common.h      # 内核态/用户态共享的数据结构
├── user/main.c               # 用户态加载器（事件消费）
├── user/include/mem_user.h   # 用户态辅助函数
├── scripts/
│   ├── gen_vmlinux.sh        # 生成 vmlinux.h
│   ├── run.sh                # 运行脚本（自动提权）
│   └── stress_alloc.sh       # 压力测试脚本
└── output/                   # 构建产物目录
```

整个项目做一件事：**实时捕获 Linux 内核中的 kmalloc/kfree 事件，通过 ring buffer 传到用户态打印**。

这是一个典型的 libbpf + CO-RE + skeleton 工程骨架，采用现代 eBPF 开发范式。

---

## 2. 构建流程 — Makefile

### 构建链路

```
vmlinux.h（内核 BTF 导出）
    ↓
mem.bpf.c  →  clang 编译  →  mem.bpf.o（BPF 字节码）
    ↓
bpftool gen skeleton  →  mem.skel.h（C skeleton 头文件）
    ↓
main.c  →  gcc 编译  →  mem-analyzer（最终可执行文件）
```

### 逐段解析

```makefile
ARCH := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')
```
自动检测 CPU 架构，转换为内核使用的架构名。eBPF 编译需要 `-D__TARGET_ARCH_xxx` 告诉 `bpf_tracing.h` 当前架构，否则 tracepoint 参数访问宏无法展开。

```makefile
BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -I./include -I/usr/include/bpf
```
- `-g`：保留调试信息（BTF 需要）
- `-O2`：必须开优化，否则 clang 可能生成 verifier 无法通过的代码
- `-target bpf`：生成 BPF 字节码而非本机指令

```makefile
$(BPF_OBJ): $(BPF_SRC) vmlinux.h include/mem_common.h | $(OUTPUT)
    $(CLANG) $(BPF_CFLAGS) -c $< -o $@
```
编译 BPF 程序。依赖 `vmlinux.h`（CO-RE 需要内核类型定义）和 `mem_common.h`（共享数据结构）。

```makefile
$(SKEL_H): $(BPF_OBJ)
    $(BPFTOOL) gen skeleton $< > $@
```
从 BPF 目标文件生成 skeleton 头文件。skeleton 把 BPF 字节码嵌入 C 头文件，用户态 `#include` 后就能直接调用 `mem_bpf__open_and_load()` 等函数，不需要手动操作 ELF。

```makefile
USER_LDFLAGS := -lbpf -lelf -lz
```
- `libbpf`：eBPF 加载库
- `libelf`：libbpf 内部解析 ELF 用
- `libz`：libbpf 内部解压缩用

---

## 3. 共享数据协议 — mem_common.h

这个头文件定义了内核态和用户态之间的"通信协议"——双方必须对数据布局达成一致。

```c
enum event_type {
    EVENT_ALLOC = 1,
    EVENT_FREE  = 2,
};
```
事件类型枚举。从 1 开始而非 0，这样 0 可以作为"无效/未初始化"的哨兵值。

```c
struct mem_event {
    __u32 pid;            // 进程 ID
    __u32 tgid;           // 线程组 ID
    char  comm[16];       // 进程名
    __u64 size;           // 分配大小（free 时为 0）
    __u64 ptr;            // 内存地址
    __u64 timestamp_ns;   // 纳秒级时间戳
    __s32 stack_id;       // 调用栈 ID（当前未实现，固定 -1）
    __u32 type;           // EVENT_ALLOC 或 EVENT_FREE
};
```

关键设计点：
- 使用 `__u32`/`__u64` 而非 `uint32_t`，因为内核态没有 `<stdint.h>`，这些类型在内核和用户态都可用
- `stack_id` 用 `__s32`（有符号），因为 `bpf_get_stackid()` 失败时返回负数
- `TASK_COMM_LEN = 16` 是 Linux 内核中进程名的固定长度上限

### pid 与 tgid 的语义问题

```c
pid_tgid = bpf_get_current_pid_tgid();
e->pid  = pid_tgid >> 32;   // 高 32 位
e->tgid = pid_tgid;          // 低 32 位（隐式截断）
```

`bpf_get_current_pid_tgid()` 返回值的布局是：**高 32 位 = tgid，低 32 位 = pid**。

但代码中把高 32 位赋给了 `e->pid`，低 32 位赋给了 `e->tgid`，**语义是反的**。实际效果：
- `e->pid` 存的其实是 tgid（用户态看到的 PID）
- `e->tgid` 存的其实是 tid（内核线程 ID）

对于单线程进程两者相同，不影响当前功能，但多线程场景下会混淆。

---

## 4. 内核态 eBPF 程序 — mem.bpf.c

这是运行在内核中的代码，由 eBPF 虚拟机执行。

### 头文件

```c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
```

- `vmlinux.h`：从当前内核 BTF 导出的所有内核类型定义，替代传统的内核头文件。这是 CO-RE 的基础——程序引用的结构体字段会在加载时由 libbpf 根据目标内核的 BTF 做重定位
- `bpf_helpers.h`：BPF helper 函数声明（`bpf_ringbuf_reserve` 等）
- `bpf_tracing.h`：tracepoint/kprobe 参数访问宏
- `bpf_core_read.h`：CO-RE 安全读取宏

### Ring Buffer Map

```c
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);  // 256KB
} events SEC(".maps");
```

Ring buffer 是内核态向用户态传递事件的通道。

为什么选 ring buffer 而非 perf buffer：
- Ring buffer 是**全局共享**的，所有 CPU 写同一个 buffer；perf buffer 是 per-CPU 的
- Ring buffer 的 `reserve/submit` 模型避免了数据拷贝——先在 buffer 中预留空间，直接写入，然后提交
- 对于高频事件（kmalloc 每秒可达数万次），ring buffer 开销更低

256KB 的容量意味着大约能缓存 `256*1024 / sizeof(mem_event)` ≈ 4000+ 个事件。如果用户态消费不及时，新事件会被丢弃。

### trace_alloc — 分配事件采集

```c
static __always_inline int trace_alloc(void *ctx, unsigned long ptr,
                                       size_t bytes_alloc)
```

`__always_inline` 是必须的——eBPF 不支持函数调用（5.10 之前），即使支持也推荐内联以减少开销。

```c
if (bytes_alloc == 0)
    return 0;
```
过滤零字节分配，这种事件没有分析价值。

```c
e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
if (!e)
    return 0;
```
在 ring buffer 中预留空间。如果 buffer 满了返回 NULL，直接丢弃事件。这里没有统计丢弃次数，是一个待改进点。

```c
e->timestamp_ns = bpf_ktime_get_ns();
```
获取内核单调时钟的纳秒时间戳，用于后续计算事件间隔或对象存活时间。

```c
e->stack_id = -1;
```
调用栈采集尚未实现。完整实现需要：
1. 添加一个 `BPF_MAP_TYPE_STACK_TRACE` 类型的 map
2. 调用 `bpf_get_stackid(ctx, &stack_map, BPF_F_FAST_STACK_CMP)` 获取 stack_id
3. 用户态通过 stack_map 查询具体的调用栈地址

```c
bpf_ringbuf_submit(e, 0);
```
提交事件，用户态立即可见。flag 为 0 表示立即通知（非延迟通知）。

### Tracepoint 挂载

```c
SEC("tracepoint/kmem/kmalloc")
int tracepoint_kmalloc(struct trace_event_raw_kmalloc *ctx)
{
    return trace_alloc(ctx, ctx->ptr, ctx->bytes_alloc);
}
```

`SEC("tracepoint/kmem/kmalloc")` 告诉 libbpf 把这个函数挂载到内核的 `kmem/kmalloc` tracepoint 上。每次内核执行 `kmalloc` 时，这个函数就会被调用。

`ctx` 的类型 `trace_event_raw_kmalloc` 来自 `vmlinux.h`，包含 tracepoint 传递的所有参数。通过 `ctx->ptr` 和 `ctx->bytes_alloc` 直接访问分配的指针和大小。

kfree 的逻辑完全对称，只是 `size` 固定为 0（kfree tracepoint 不提供释放大小）。

### License 声明

```c
char LICENSE[] SEC("license") = "GPL";
```
必须声明 GPL，否则无法使用 `bpf_get_stackid` 等 GPL-only 的 helper 函数。

---

## 5. 用户态加载器 — main.c

### Skeleton 工作流

```c
skel = mem_bpf__open_and_load();
```
这一行做了三件事：
1. **open**：解析嵌入在 skeleton 中的 BPF ELF，创建 BPF 对象
2. **load**：将 BPF 字节码加载到内核，经过 verifier 验证
3. 返回 skeleton 指针，后续通过它访问 maps 和 programs

```c
err = mem_bpf__attach(skel);
```
将 BPF 程序 attach 到对应的 tracepoint。attach 之后，每次 kmalloc/kfree 发生时 BPF 程序就会执行。

### Ring Buffer 消费

```c
rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
```
创建 ring buffer 消费者，绑定到 `events` map，注册 `handle_event` 作为回调。

```c
while (!exiting) {
    err = ring_buffer__poll(rb, 100);  // 100ms 超时
    if (err == -EINTR) {
        err = 0;
        break;
    }
}
```
主循环：每 100ms 轮询一次 ring buffer。`poll` 内部使用 `epoll`，不是忙等待。收到信号时 `poll` 返回 `-EINTR`，配合 `sig_handler` 设置的 `exiting` 标志实现优雅退出。

### 事件处理

```c
static int handle_event(void *ctx, void *data, size_t data_sz)
{
    const struct mem_event *e = data;
    printf("%-7s  pid=%-6u  comm=%-16s  ptr=0x%-14llx  size=%-8llu  ts=%llu\n",
           event_type_str(e->type), e->pid, e->comm, e->ptr, e->size,
           e->timestamp_ns);
    return 0;
}
```
当前只做原始打印。每个事件到达时打印类型、PID、进程名、指针地址、大小和时间戳。

### 资源清理

```c
cleanup:
    ring_buffer__free(rb);
    mem_bpf__destroy(skel);
```
`ring_buffer__free` 释放用户态 ring buffer 消费者。`mem_bpf__destroy` 会 detach BPF 程序、关闭所有 map fd、释放内存。顺序很重要——先释放 rb（它持有 map fd），再销毁 skel。

---

## 6. 用户态辅助 — mem_user.h

```c
static const char *event_type_str(__u32 type)
{
    switch (type) {
    case EVENT_ALLOC: return "ALLOC";
    case EVENT_FREE:  return "FREE";
    default:          return "UNKNOWN";
    }
}
```

简单的枚举转字符串。声明为 `static` 避免多文件包含时的链接冲突。

---

## 7. 辅助脚本

### gen_vmlinux.sh — 生成内核类型头文件

```bash
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
```
从内核导出的 BTF 信息生成 C 头文件。这个文件包含当前内核所有结构体、枚举、typedef 的定义，通常有 10 万行以上。CO-RE 的核心依赖。

前置条件：内核编译时需要开启 `CONFIG_DEBUG_INFO_BTF=y`。

### run.sh — 运行脚本

检查二进制是否存在，非 root 时自动 `sudo` 提权。eBPF 程序加载需要 `CAP_BPF`（或 root），这个脚本简化了运行流程。

### stress_alloc.sh — 压力测试

```bash
for i in $(seq 1 1000); do
    dd if=/dev/zero of=/tmp/stress_alloc_$i bs=4096 count=10 2>/dev/null
done
```
通过大量文件 I/O 触发内核内存分配。`dd` 写文件会触发 page cache 分配、inode 分配、dentry 分配等，间接产生大量 kmalloc 调用。用于验证工具在高负载下的表现。

---

## 8. 数据流全链路图

```
内核空间                                    用户空间
─────────────────────────────────────────────────────────────

  进程调用 kmalloc(size)
       │
       ▼
  tracepoint/kmem/kmalloc 触发
       │
       ▼
  tracepoint_kmalloc() 执行
       │
       ├─ bpf_get_current_pid_tgid()  → 获取进程信息
       ├─ bpf_get_current_comm()      → 获取进程名
       ├─ bpf_ktime_get_ns()          → 获取时间戳
       │
       ▼
  bpf_ringbuf_reserve()
       │ 在 ring buffer 中预留空间
       ▼
  填充 mem_event 结构体
       │
       ▼
  bpf_ringbuf_submit()
       │                               ring_buffer__poll()
       │         ┌─── ring buffer ───┐       │
       └────────►│  mem_event 队列   │───────►│
                 └───────────────────┘       │
                                             ▼
                                       handle_event()
                                             │
                                             ▼
                                        printf 输出
```

---

## 9. 已知问题与改进方向

| 问题 | 说明 | 影响 |
|------|------|------|
| pid/tgid 语义反转 | `pid_tgid >> 32` 是 tgid，却赋给了 `e->pid` | 多线程场景下统计不准 |
| 无丢事件统计 | ring buffer 满时静默丢弃，用户无感知 | 高负载下可能丢失关键事件而不自知 |
| stack_id 未实现 | 固定为 -1，缺少 STACK_TRACE map | 无法定位分配热点的具体调用路径 |
| 仅原始打印 | 没有聚合、统计、分析能力 | 工具价值有限，无法直接用于问题定位 |
| kfree 无 size | tracepoint 不提供释放大小 | 无法直接统计释放字节数 |
| 覆盖点有限 | 只 hook kmalloc/kfree，缺少 kmem_cache_alloc/free | 遗漏 slab 分配器的大量分配事件 |
| ring buffer 容量固定 | 256KB 硬编码，无法运行时调整 | 高频场景容易溢出 |
