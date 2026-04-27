CLANG      ?= clang
LLC        ?= llc
BPFTOOL    ?= bpftool
CC         ?= gcc

ARCH       := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')
BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) \
              -I./include -I/usr/include/bpf
USER_CFLAGS := -g -Wall -I./include -I.
USER_LDFLAGS := -lbpf -lelf -lz

OUTPUT     := output
BPF_SRC    := bpf/mem.bpf.c
BPF_OBJ    := $(OUTPUT)/mem.bpf.o
SKEL_H     := user/mem.skel.h
USER_SRC   := user/main.c
USER_BIN   := $(OUTPUT)/mem-analyzer

.PHONY: all clean vmlinux

all: $(USER_BIN)

$(OUTPUT):
	mkdir -p $(OUTPUT)

vmlinux:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

$(BPF_OBJ): $(BPF_SRC) vmlinux.h include/mem_common.h | $(OUTPUT)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

$(SKEL_H): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

$(USER_BIN): $(USER_SRC) $(SKEL_H) user/include/mem_user.h | $(OUTPUT)
	$(CC) $(USER_CFLAGS) -o $@ $< $(USER_LDFLAGS)

clean:
	rm -rf $(OUTPUT) $(SKEL_H)
