/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MemSnoop - eBPF Memory Analysis Engine
 * Copyright (c) 2025 kkst
 *
 * user/include/symbol.h - Kernel symbol resolution interface
 */
#ifndef __SYMBOL_H
#define __SYMBOL_H

#include <stdint.h>

/*
 * Load kernel symbols from /proc/kallsyms.
 * Returns 0 on success, -1 on failure (e.g. permission denied).
 * Symbols are sorted by address for binary search.
 */
int symbols_load(void);

/* Free all loaded symbols. */
void symbols_free(void);

/*
 * Resolve a kernel address to a symbol name.
 * Returns the symbol name string, or NULL if not found.
 * The returned pointer is valid until symbols_free() is called.
 */
const char *symbol_resolve(uint64_t addr);

/*
 * Check if symbols were successfully loaded.
 * Returns 1 if loaded, 0 otherwise.
 */
int symbols_loaded(void);

/*
 * Print a warning about symbol resolution status (e.g. kptr_restrict).
 */
void symbols_print_status(void);

#endif /* __SYMBOL_H */
