# MemSnoop — 基于 eBPF 的 Linux 内核内存行为分析引擎

## 项目简介

使用 libbpf + CO-RE + BPF skeleton 构建的内核内存行为分析工具。挂载 `kmalloc`/`kfree` tracepoint 采集内存分配/释放事件，通过 ring buffer 上报用户态，支持 PID 统计、分配大小 histogram、内核调用栈热点分析、符号解析和疑似泄漏检测。

## 核心功能

- **事件采集**：挂载 `tracepoint/kmem/kmalloc` 和 `tracepoint/kmem/kfree`，采集 pid、size、ptr、timestamp、stack_id
- **Ring Buffer 传输**：4MB ring buffer 多 CPU 无锁传输，含丢事件监控
- **PID 统计**：按进程统计 alloc_count、free_count、active_bytes，支持 TopN 排序
- **Histogram**：log2 bucket 分配大小分布（0-64B 到 256KB+），ASCII 条形图
- **Stack Trace 热点**：`BPF_MAP_TYPE_STACK_TRACE` 采集内核调用栈，按 stack_id 聚合热点
- **符号解析**：读取 `/proc/kallsyms`，将栈地址解析为函数名（支持 kptr_restrict 降级提示）
- **疑似泄漏检测**：扫描长时间未释放的分配对象，按调用栈分组，支持阈值配置

## 环境要求

- Linux 内核 >= 5.8（需支持 BTF 和 ring buffer）
- clang >= 11
- llvm
- libbpf-dev（或从源码编译 libbpf >= 0.8）
- bpftool
- libelf-dev, zlib1g-dev
- make, gcc

### Ubuntu/Debian 安装依赖

```bash
sudo apt update
sudo apt install -y clang llvm libbpf-dev bpftool \
    libelf-dev zlib1g-dev linux-headers-$(uname -r) make gcc
```

### 确认内核 BTF 支持

```bash
ls /sys/kernel/btf/vmlinux
```

## 构建

```bash
# 构建主程序（自动生成 vmlinux.h、BPF 对象、skeleton）
make

# 构建测试程序（泄漏检测验证用）
make test
```

## 运行

```bash
# 需要 root 权限
sudo ./output/memsnoop [OPTIONS]

# 或使用脚本
bash scripts/run.sh
```

### CLI 参数

```
Usage: memsnoop [OPTIONS]

Options:
  --pid <pid>        仅追踪指定进程
  --top <n>          显示 Top N 进程（默认: 10）
  --interval <sec>   刷新间隔秒数（默认: 1）
  --duration <sec>   运行指定秒数后退出（0=持续运行）
  --hist             显示分配大小 histogram
  --stack            显示 TopN 分配调用栈热点
  --leak             显示疑似内存泄漏
  --leak-after <sec> 泄漏检测阈值秒数（默认: 10）
  --min-size <bytes> 忽略小于此大小的分配（默认: 0）
  --verbose          打印每个事件到 stdout
  --help             显示帮助
```

### 使用示例

```bash
# 基本运行：TopN 进程统计 + ring buffer 监控
sudo ./output/memsnoop

# 开启 histogram 和调用栈热点
sudo ./output/memsnoop --hist --stack

# 泄漏检测：追踪超过 30 秒未释放的分配
sudo ./output/memsnoop --leak --leak-after 30

# 仅追踪特定进程，每 5 秒刷新
sudo ./output/memsnoop --pid 1234 --interval 5

# 完整分析模式
sudo ./output/memsnoop --hist --stack --leak --leak-after 10 --min-size 256
```

## 示例输出

```
MemSnoop v0.3 - eBPF Memory Analysis Engine (by kkst)
  Symbols: 123456 kernel symbols loaded from /proc/kallsyms
Tracing kmalloc/kfree...
  interval=1s  top=10  pid=0  hist=on  stack=on  leak=on
Press Ctrl-C to stop.

────────────────────────────────────────────────────────────────
  MemSnoop report  [2025-01-15 10:30:45]
────────────────────────────────────────────────────────────────

  [Top 10 Processes by Active Memory]

  PID       ALLOCS      FREES       ALLOC_BYTES   FREE_BYTES    ACTIVE
  1234      5432        5100        2.1MB         1.8MB         320.0KB
  567       1234        1200        512.0KB       480.0KB       32.0KB

  [Allocation Size Histogram]

  SIZE_RANGE    COUNT       DISTRIBUTION
  0-64B         3421        |########################################
  64-256B       2100        |########################
  256B-1KB      890         |##########
  1KB-4KB       320         |###
  4KB-16KB      120         |#
  16KB-64KB     45          |
  64KB-256KB    10          |
  256KB+        2           |
  Total allocations: 6908

  [Top 5 Allocation Stacks (by active bytes)]

  #1  stack_id=42     256.0KB (128 allocs)
       [0] __kmalloc
       [1] load_elf_binary
       [2] load_misc_binary
       [3] search_binary_handler

  [Suspected Leaks (age > 10s, min_size=0)]

  #1  pid=1234   128.0KB (32 objs, oldest=45s)  stack_id=15
       [0] __kmalloc
       [1] kvmalloc_node
       [2] __alloc_skb

  [Ring Buffer Stats]
  Events: total=15234  dropped=12  drop_rate=0.08%
────────────────────────────────────────────────────────────────
```

## 符号解析说明

符号解析依赖 `/proc/kallsyms`，需要 root 权限。如果 `kptr_restrict >= 1`（某些发行版默认值），地址会显示为 0x0，此时需要：

```bash
# 方法 1：以 root 运行 memsnoop（推荐）
sudo ./output/memsnoop

# 方法 2：临时放开 kptr_restrict
echo 0 | sudo tee /proc/sys/kernel/kptr_restrict
```

MemSnoop 启动时会自动检测并提示符号解析状态。

## 压力测试

```bash
# 运行 30 秒压力测试（5 种模式并发）
bash scripts/stress_alloc.sh 30
```

压力测试包含 5 种模式：
1. 小对象高频分配（512B dd 块）
2. 大块分配（64KB dd 块）
3. 快速文件创建/删除（alloc+free 交替）
4. 网络 socket 分配（curl localhost）
5. 用户态 malloc 泄漏模拟（需先 `make test`）

## 工程结构

```
ebpf-code/
├── Makefile                    # 构建系统
├── README.md
├── vmlinux.h                   # 内核 BTF 类型定义
├── bpf/
│   └── mem.bpf.c               # 内核态 eBPF 程序（7 个 map + 2 个 tracepoint）
├── include/
│   └── mem_common.h            # 内核/用户态共享数据结构
├── user/
│   ├── main.c                  # 用户态主程序：加载、事件消费、报告循环
│   ├── stats.c                 # 统计模块：PID TopN、histogram、stack、泄漏检测
│   ├── symbol.c                # 符号解析：/proc/kallsyms 读取与二分查找
│   └── include/
│       ├── mem_user.h          # CLI 解析、工具函数
│       ├── stats.h             # 统计模块接口
│       └── symbol.h            # 符号解析接口
├── scripts/
│   ├── gen_vmlinux.sh          # 生成 vmlinux.h
│   ├── run.sh                  # 运行脚本（自动 sudo）
│   └── stress_alloc.sh         # 压力测试脚本
├── test_leak.c                 # 泄漏检测测试程序
└── docs/
    ├── week1.md
    ├── week2.md
    └── code-analysis.md
```

## 技术栈

C / eBPF / libbpf / CO-RE / BPF skeleton / Ring Buffer / Linux Tracepoint

## License

GPL-2.0
