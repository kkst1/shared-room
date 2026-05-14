# ebpf-code 代码审查报告

审查日期：2026-05-14
审查范围：第 6 周（符号解析）和第 7 周（泄漏检测）新增代码，以及全部已有代码
审查方式：逐行代码审查 + 实际编译验证

---

## 一、编译错误（必须修复）

### 1.1 trace_event_raw 结构体重定义

**文件**：`bpf/mem.bpf.c`
**严重度**：编译错误
**现象**：

```
bpf/mem.bpf.c:15:8: error: redefinition of 'trace_event_raw_kmalloc'
struct trace_event_raw_kmalloc {
       ^
./vmlinux.h:139069:8: note: previous definition is here
```

**原因**：代码手动定义了 `trace_event_raw_kmalloc` 和 `trace_event_raw_kfree`，但 vmlinux.h（从运行内核 BTF 自动生成）中已有这两个结构体。手动定义导致重定义冲突。

**修复**：删除两个手动结构体定义。vmlinux.h 已包含它们，且项目要求 BTF 支持（Makefile 从 `/sys/kernel/btf/vmlinux` 生成 vmlinux.h），手动定义完全多余。

**修改内容**：
```c
// 删除以下代码
struct trace_event_raw_kmalloc {
    struct trace_entry ent;
    long unsigned int call_site;
    const void *ptr;
    size_t bytes_req;
    size_t bytes_alloc;
    gfp_t gfp_flags;
    int node;
};

struct trace_event_raw_kfree {
    struct trace_entry ent;
    long unsigned int call_site;
    const void *ptr;
};
```

**注意**：vmlinux.h 中的定义与手动定义略有不同（`gfp_flags` 类型为 `long unsigned int` 而非 `gfp_t`，且末尾有 `char __data[0]`）。删除手动定义后，代码自动使用 vmlinux.h 的正确定义。

---

### 1.2 STATS_EVENTS 宏未在共享头文件中定义

**文件**：`user/stats.c`（使用处）、`bpf/mem.bpf.c`（原定义处）
**严重度**：编译错误
**现象**：

```
user/stats.c:254:11: error: 'STATS_EVENTS_TOTAL' undeclared
user/stats.c:259:11: error: 'STATS_EVENTS_DROPPED' undeclared
```

**原因**：`STATS_EVENTS_TOTAL`（0）和 `STATS_EVENTS_DROPPED`（1）宏原定义在 `bpf/mem.bpf.c` 中，但用户态 `stats.c` 的 `print_drop_stats()` 函数也需要使用它们。BPF 代码和用户态代码不共享同一个源文件。

**修复**：
1. 将两个宏移至 `include/mem_common.h`（内核/用户态共享头文件）
2. 删除 `bpf/mem.bpf.c` 中的重复定义

**修改内容**：

`include/mem_common.h` 新增：
```c
#define STATS_EVENTS_TOTAL   0
#define STATS_EVENTS_DROPPED 1
```

`bpf/mem.bpf.c` 删除：
```c
#define STATS_EVENTS_TOTAL   0
#define STATS_EVENTS_DROPPED 1
```

---

## 二、运行时逻辑缺陷（建议修复）

### 2.1 symbol_resolve 在 kptr_restrict 下返回错误符号

**文件**：`user/symbol.c`
**严重度**：高（输出错误信息）
**现象**：当 `kptr_restrict >= 1` 时，`/proc/kallsyms` 中所有地址显示为 `0x0`。去重后只剩一个 addr=0 的条目。`symbol_resolve()` 对任何查询地址都会返回该符号名，导致输出完全错误的函数名。

**修复**：在 `symbol_resolve()` 开头增加 kptr_restrict 检查：

```c
const char *symbol_resolve(uint64_t addr)
{
    if (!syms || sym_count == 0 || addr == 0)
        return NULL;

    // 新增：kptr_restrict 活跃时，所有符号地址为 0，解析结果不可信
    if (load_status == -2)
        return NULL;

    // ... 原有二分查找逻辑
}
```

### 2.2 STATS_EVENTS 宏定义位置不当导致 stats.c 无法编译

**文件**：`user/stats.c`
**严重度**：编译错误（已在 1.2 中修复）
**说明**：此问题与 1.2 相同，从 stats.c 视角描述。

### 2.3 symbols_print_status 输出到 stderr 导致顺序混乱

**文件**：`user/symbol.c`
**严重度**：低（显示问题）
**现象**：`symbols_print_status()` 使用 `fprintf(stderr, ...)` 输出，而启动 banner 使用 `printf`（stdout）。stdout 和 stderr 的缓冲策略不同，导致输出顺序不可预测。

**修复**：改为 `printf` 输出到 stdout。

### 2.4 print_leak_suspects 中 pid 字段误导

**文件**：`user/stats.c`、`user/include/stats.h`
**严重度**：中（信息误导）
**现象**：`leak_entry` 结构体有 `pid` 字段，`print_leak_suspects()` 按 `stack_id` 分组但只记录第一个遇到的 PID。同一内核栈可被不同进程触发，显示单个 PID 会误导用户以为泄漏只来自一个进程。

**修复**：
1. 从 `struct leak_entry` 中删除 `pid` 字段
2. 从输出格式中删除 pid 显示
3. 以 stack_id 和调用栈作为泄漏分组的主要标识

### 2.5 print_age_human 格式字符串类型不匹配

**文件**：`user/stats.c`
**严重度**：低（运行时正确，编译器告警）
**现象**：`%lu` 格式符期望 `unsigned long`，但参数是 `unsigned long long`。在 x86_64 上两者都是 64 位，运行时正确。

**修复**：统一使用 `%llu` 和 `(unsigned long long)` 强制转换。

---

## 三、编译警告（非致命）

### 3.1 PRIu64/PRIx64 与 __u64 类型不匹配

**文件**：所有使用 `PRIu64`/`PRIx64` 的文件
**严重度**：低（运行时正确）
**现象**：

```
warning: format '%lu' expects argument of type 'long unsigned int',
but argument has type '__u64' {aka 'long long unsigned int'}
```

**原因**：libbpf 定义 `__u64` 为 `unsigned long long`，但 glibc 的 `PRIu64` 宏展开为 `"lu"`（对应 `unsigned long`）。在 x86_64 上两者都是 64 位，ABI 兼容，运行时无问题。

**修复**：Makefile 中添加 `-Wno-format` 抑制此系统性警告。

### 3.2 头文件 static 函数未使用警告

**文件**：`user/include/mem_user.h`
**严重度**：低
**现象**：

```
warning: 'print_size_human' defined but not used
warning: 'hist_bucket_str' defined but not used
warning: 'parse_opts' defined but not used
warning: 'event_type_str' defined but not used
```

**原因**：这些函数定义为 `static` 并放在头文件中。`main.c` 不使用 `print_size_human`/`hist_bucket_str`，`stats.c` 不使用 `parse_opts`/`event_type_str`，编译器对未使用的 `static` 函数发出警告。

**修复**：未修改。这是已有代码的设计选择（头文件中定义 static 函数），修改会影响代码结构。

---

## 四、已有代码潜在问题（非新引入）

### 4.1 无符号减法下溢风险

**文件**：`user/stats.c`（`cmp_pid_entry`、`cmp_stack_entry`、`print_pid_stats_top`）
**现象**：

```c
__u64 active = entries[i].total_alloc_bytes - entries[i].total_free_bytes;
```

`__u64` 是无符号类型。如果 `total_free_bytes > total_alloc_bytes`（读取 map 时内核正在更新的竞态窗口下可能），减法下溢为极大正数，导致排序错误。

**评估**：实际概率极低（需要精确命中内核更新窗口），但代码不够防御。

### 4.2 VLA 栈溢出风险

**文件**：`user/stats.c`（`print_histogram`）
**现象**：

```c
__u64 percpu_vals[num_cpus];  // VLA
```

`libbpf_num_possible_cpus()` 在高 CPU 数系统上可能返回较大值。512 CPUs = 4KB 栈空间，通常安全，但 2048+ CPUs 可能溢出。

### 4.3 pid_entry.comm 字段从未填充

**文件**：`user/stats.c`、`user/include/mem_user.h`
**现象**：`struct pid_entry` 有 `comm[TASK_COMM_LEN]` 字段，`print_pid_stats_top()` 中 `memset` 为零但从未从 BPF 获取实际进程名。`pid_stats_map` 的值类型 `struct pid_stats` 本身不含 `comm` 字段。

### 4.4 test_leak 的 malloc 不一定触发 kmalloc

**文件**：`test_leak.c`
**现象**：`malloc(alloc_size)` 由 glibc 管理。小分配（默认 < 128KB）走 brk/sbrk，glibc arena 缓存可能复用已有堆空间，不一定每次触发内核 `kmalloc` tracepoint。MemSnoop 可能看不到预期数量的分配事件。

---

## 五、修改文件清单

| 文件 | 修改类型 | 修改内容 |
|------|----------|----------|
| `bpf/mem.bpf.c` | 删除 | 手动定义的 `trace_event_raw_kmalloc` 和 `trace_event_raw_kfree` |
| `bpf/mem.bpf.c` | 删除 | `STATS_EVENTS_TOTAL`/`STATS_EVENTS_DROPPED` 宏定义（移至共享头文件） |
| `include/mem_common.h` | 新增 | `STATS_EVENTS_TOTAL`/`STATS_EVENTS_DROPPED` 宏定义 |
| `user/symbol.c` | 新增文件 | 符号解析模块（/proc/kallsyms 读取、二分查找、kptr_restrict 处理） |
| `user/include/symbol.h` | 新增文件 | 符号解析接口声明 |
| `user/stats.c` | 修改 | `print_stack_top()` 集成符号解析 |
| `user/stats.c` | 新增 | `print_leak_suspects()`、`print_age_human()`、`cmp_leak_entry()` |
| `user/include/stats.h` | 修改 | 新增 `struct leak_entry`、`print_leak_suspects()` 声明 |
| `user/include/mem_user.h` | 修改 | 新增 `show_leak`/`leak_after_sec`/`min_leak_size` 字段和 CLI 解析 |
| `user/main.c` | 修改 | 集成符号加载/释放、泄漏检测报告输出、版本号升级 v0.3 |
| `Makefile` | 修改 | 新增 `symbol.c` 编译、`test` 目标、`-Wno-format`、依赖声明 |
| `test_leak.c` | 新增文件 | 泄漏检测测试程序 |
| `scripts/stress_alloc.sh` | 修改 | 新增第 5 种模式（用户态 malloc 泄漏模拟） |
| `README.md` | 重写 | 全面更新，覆盖所有功能和使用说明 |

---

## 六、编译验证结果

```
$ make clean && make && make test

BPF 编译     ✅  output/mem.bpf.o (22896 bytes)
Skeleton     ✅  user/mem.skel.h
主程序       ✅  output/memsnoop (86824 bytes)
测试程序     ✅  output/test_leak (20480 bytes)
CLI --help   ✅  所有新选项正确显示

剩余警告：
  -Wunused-function   4 个（头文件 static 函数设计，非新引入）
  -Wformat            已通过 -Wno-format 抑制（libbpf 平台兼容性问题）
```
