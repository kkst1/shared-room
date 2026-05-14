# 第 6 周 & 第 7 周实现记录

## 第 6 周：符号解析与可读输出

### 新增文件

**user/include/symbol.h** — 符号解析接口
- `symbols_load()`：从 `/proc/kallsyms` 加载内核符号，排序去重
- `symbol_resolve(addr)`：二分查找地址对应的函数名
- `symbols_print_status()`：打印符号加载状态和 kptr_restrict 警告
- `symbols_free()`：释放符号表内存

**user/symbol.c** — 符号解析实现
- 读取 `/proc/kallsyms`，解析 `address type name` 格式
- 动态数组存储，支持 realloc 扩容
- qsort 按地址排序，去重保留首个符号
- 二分查找：找到 `<= addr` 的最大符号地址
- kptr_restrict 检测：所有地址为 0 时设 `load_status = -2`，`symbol_resolve()` 直接返回 NULL

### 修改文件

**user/stats.c** — `print_stack_top()` 集成符号解析
```c
// 原始：只输出原始地址
printf("       [%d] 0x%" PRIx64 "\n", j, stack_addrs[j]);

// 修改后：优先输出函数名，找不到时回退到地址
const char *sym = symbol_resolve(stack_addrs[j]);
if (sym)
    printf("       [%d] %s\n", j, sym);
else
    printf("       [%d] 0x%" PRIx64 "\n", j, stack_addrs[j]);
```

**user/main.c** — 启动时加载符号，退出时释放
```c
// main() 中
symbols_load();           // 在 mem_bpf__open() 之前
symbols_print_status();   // 启动 banner 中

// cleanup 中
symbols_free();           // 在 mem_bpf__destroy() 之后
```

---

## 第 7 周：疑似泄漏检测

### 新增文件

**test_leak.c** — 泄漏检测测试程序
- `malloc()` 分配内存，`memset` 写入确保实际分配
- 可配置参数：`--size`、`--count`、`--interval`、`--hold`
- 持有分配指定时间后释放
- 用途：配合 `memsnoop --leak --leak-after <sec>` 验证泄漏检测

### 修改文件

**user/include/mem_user.h** — 新增 CLI 选项
```c
struct cli_opts {
    // ... 原有字段 ...
    bool  show_leak;        // --leak
    int   leak_after_sec;   // --leak-after <sec>，默认 10
    __u64 min_leak_size;    // --min-size <bytes>，默认 0
};
```

**user/include/stats.h** — 新增泄漏结构体和函数声明
```c
struct leak_entry {
    __s32 stack_id;
    __u64 total_bytes;
    __u64 count;
    __u64 oldest_age_ns;
};

int print_leak_suspects(int stack_fd, int alloc_fd,
                        __u64 min_age_ns, __u64 min_size, int top_n);
```

**user/stats.c** — 实现泄漏检测逻辑
- 遍历 `alloc_table`，计算每条分配的年龄（`now_ns - timestamp_ns`）
- 过滤条件：`age >= min_age_ns && size >= min_size`
- 按 `stack_id` 分组聚合：`total_bytes`、`count`、`oldest_age_ns`
- 按 `total_bytes` 降序排序，输出 TopN
- 每组附带符号解析的内核调用栈

**user/main.c** — 集成泄漏检测报告
```c
if (opts.show_leak) {
    __u64 min_age_ns = (__u64)opts.leak_after_sec * 1000000000ULL;
    print_leak_suspects(bpf_map__fd(skel->maps.stack_traces),
                        bpf_map__fd(skel->maps.alloc_table),
                        min_age_ns, opts.min_leak_size, opts.top_n);
}
```

**Makefile** — 构建更新
- `USER_SRCS` 新增 `user/symbol.c`
- 依赖声明新增 `user/include/symbol.h`
- 新增 `make test` 目标编译 `test_leak.c`

**scripts/stress_alloc.sh** — 新增第 5 种测试模式
```bash
# Pattern 5: Userspace malloc leak simulation
if [ -x "$TEST_LEAK" ]; then
    "$TEST_LEAK" --size 4096 --count 20 --interval 100 --hold "$DURATION" &
fi
```

---

## 关键设计决策

### 符号解析：为什么不用 BTF？
- BTF 可以解析类型信息，但函数名解析需要 `btf__get_nr_funcs()` 等 API，libbpf 0.5.0 不一定支持
- `/proc/kallsyms` 是最通用的方案，支持所有内核版本
- 缺点：需要 root 权限，受 kptr_restrict 限制

### 泄漏检测：为什么在用户态做？
- BPF 端只能做轻量采集（verifier 限制）
- 泄漏判断需要时间比较、分组聚合、排序，这些逻辑放在用户态更合适
- `alloc_table` 已有 `timestamp_ns` 和 `stack_id`，无需在 BPF 端新增逻辑

### 泄漏检测：为什么按 stack_id 分组而不是 ptr？
- 单个 ptr 的泄漏信息价值有限
- 按 stack_id 分组可以看到"哪条代码路径产生了最多未释放内存"
- 这是定位泄漏根因的关键信息

### 符号解析 + 泄漏检测：为什么不在 BPF 端做符号解析？
- eBPF verifier 限制：不允许循环、函数调用深度有限
- 符号表查找需要二分查找（循环），无法在 BPF 中实现
- `/proc/kallsyms` 文件读取只能在用户态完成
