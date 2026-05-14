// SPDX-License-Identifier: GPL-2.0
/*
 * MemSnoop - eBPF Memory Analysis Engine
 * Copyright (c) 2025 kkst
 *
 * user/symbol.c - Kernel symbol resolution via /proc/kallsyms
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "include/symbol.h"

struct ksym {
    uint64_t addr;
    char *name;
};

static struct ksym *syms = NULL;
static int sym_count = 0;
static int sym_capacity = 0;
static int load_status = 0; /* 0=not loaded, 1=loaded, -1=failed */

static int cmp_sym(const void *a, const void *b)
{
    const struct ksym *sa = a;
    const struct ksym *sb = b;

    if (sa->addr < sb->addr) return -1;
    if (sa->addr > sb->addr) return 1;
    return 0;
}

int symbols_load(void)
{
    FILE *f;
    char line[512];
    int all_zero = 1;

    if (syms) {
        symbols_free();
    }

    f = fopen("/proc/kallsyms", "r");
    if (!f) {
        load_status = -1;
        return -1;
    }

    sym_capacity = 4096;
    sym_count = 0;
    syms = malloc(sizeof(struct ksym) * sym_capacity);
    if (!syms) {
        fclose(f);
        load_status = -1;
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        uint64_t addr;
        char type;
        char name[256];

        if (sscanf(line, "%llx %c %255s", (unsigned long long *)&addr,
                   &type, name) < 3)
            continue;

        if (addr != 0)
            all_zero = 0;

        if (sym_count >= sym_capacity) {
            sym_capacity *= 2;
            struct ksym *tmp = realloc(syms, sizeof(struct ksym) * sym_capacity);
            if (!tmp) {
                /* Continue with what we have */
                break;
            }
            syms = tmp;
        }

        syms[sym_count].addr = addr;
        syms[sym_count].name = strdup(name);
        if (!syms[sym_count].name)
            continue;
        sym_count++;
    }

    fclose(f);

    if (sym_count == 0) {
        free(syms);
        syms = NULL;
        load_status = -1;
        return -1;
    }

    /* Check if all addresses are zero (kptr_restrict) */
    if (all_zero) {
        load_status = -2;
        /* Don't free - we can still try, but warn user */
    }

    qsort(syms, sym_count, sizeof(syms[0]), cmp_sym);

    /* Deduplicate: keep first occurrence of each address */
    int unique = 0;
    for (int i = 1; i < sym_count; i++) {
        if (syms[i].addr != syms[unique].addr) {
            unique++;
            syms[unique] = syms[i];
        } else {
            free(syms[i].name);
        }
    }
    sym_count = unique + 1;

    if (load_status != -2)
        load_status = 1;

    return 0;
}

void symbols_free(void)
{
    if (!syms)
        return;

    for (int i = 0; i < sym_count; i++) {
        free(syms[i].name);
    }
    free(syms);
    syms = NULL;
    sym_count = 0;
    sym_capacity = 0;
    load_status = 0;
}

const char *symbol_resolve(uint64_t addr)
{
    if (!syms || sym_count == 0 || addr == 0)
        return NULL;

    /* When kptr_restrict is active, all symbol addresses are 0.
     * Resolving any address against them would return a wrong symbol.
     */
    if (load_status == -2)
        return NULL;

    /* Binary search: find largest symbol address <= addr */
    int lo = 0, hi = sym_count - 1;
    int result = -1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;

        if (syms[mid].addr <= addr) {
            result = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (result < 0)
        return NULL;

    return syms[result].name;
}

int symbols_loaded(void)
{
    return (load_status == 1 || load_status == -2) && syms != NULL;
}

void symbols_print_status(void)
{
    switch (load_status) {
    case 0:
        printf("  Warning: symbols not loaded\n");
        break;
    case 1:
        printf("  Symbols: %d kernel symbols loaded from /proc/kallsyms\n",
               sym_count);
        break;
    case -1:
        printf("  Warning: failed to read /proc/kallsyms "
               "(permission denied?). Stack traces will show raw addresses.\n");
        break;
    case -2:
        printf("  Warning: /proc/kallsyms addresses are all zero "
               "(kptr_restrict >= 1). Run with sudo or:\n"
               "    echo 0 > /proc/sys/kernel/kptr_restrict\n"
               "  Stack traces will show raw addresses.\n");
        break;
    }
}
