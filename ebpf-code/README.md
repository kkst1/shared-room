# mem-analyzer — 基于 eBPF 的 Linux 内核内存行为分析工具

## 项目简介

使用 libbpf + CO-RE + BPF skeleton 构建的内核内存事件采集工具。
当前（第 1 周）实现了最小闭环：挂载 `kmalloc`/`kfree` tracepoint，通过 ring buffer 将事件上报用户态并格式化输出。

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
# 1. 生成 vmlinux.h（首次构建需要）
bash scripts/gen_vmlinux.sh

# 2. 编译
make
```

## 运行

```bash
# 需要 root 权限
sudo ./output/mem-analyzer

# 或使用脚本
bash scripts/run.sh
```

## 示例输出

```
mem-analyzer started. Tracing kmalloc/kfree... Ctrl-C to stop.
TYPE     PID        COMM               PTR                SIZE        TIMESTAMP
ALLOC    pid=1234   comm=bash           ptr=0xffff8881234  size=128      ts=123456789
FREE     pid=1234   comm=bash           ptr=0xffff8881234  size=0        ts=123456800
```
