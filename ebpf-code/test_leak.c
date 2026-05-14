// SPDX-License-Identifier: GPL-2.0
/*
 * MemSnoop - test_leak.c
 * Test program that intentionally leaks memory to verify leak detection.
 *
 * Usage: ./test_leak [--size <bytes>] [--count <n>] [--interval <sec>]
 *
 * This program allocates memory via malloc() without freeing, creating
 * "leaked" objects that MemSnoop's --leak mode should detect.
 *
 * NOTE: This is a deliberate test tool, not a bug.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  --size <bytes>    Size of each allocation (default: 1024)\n"
        "  --count <n>       Number of allocations to make (default: 50)\n"
        "  --interval <ms>   Milliseconds between allocations (default: 200)\n"
        "  --hold <sec>      Hold allocations for this many seconds (default: 60)\n"
        "  --help            Show this help\n",
        prog);
}

int main(int argc, char **argv)
{
    size_t alloc_size = 1024;
    int count = 50;
    int interval_ms = 200;
    int hold_sec = 60;

    static struct option long_opts[] = {
        {"size",     required_argument, 0, 's'},
        {"count",    required_argument, 0, 'c'},
        {"interval", required_argument, 0, 'i'},
        {"hold",     required_argument, 0, 'h'},
        {"help",     no_argument,       0, '?'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:c:i:h:", long_opts, NULL)) != -1) {
        switch (opt) {
        case 's': alloc_size = (size_t)atoi(optarg); break;
        case 'c': count = atoi(optarg); break;
        case 'i': interval_ms = atoi(optarg); break;
        case 'h': hold_sec = atoi(optarg); break;
        default:  usage(argv[0]); return 1;
        }
    }

    printf("test_leak: size=%zu count=%d interval=%dms hold=%ds\n",
           alloc_size, count, interval_ms, hold_sec);
    printf("Leaking %d allocations of %zu bytes each (total ~%zu KB)\n",
           count, alloc_size, (size_t)count * alloc_size / 1024);
    printf("Hold for %d seconds, then exit.\n", hold_sec);
    printf("Run MemSnoop with --leak --leak-after <sec> to detect.\n\n");

    void **ptrs = malloc(sizeof(void *) * count);
    if (!ptrs) {
        fprintf(stderr, "Failed to allocate pointer array\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        ptrs[i] = malloc(alloc_size);
        if (!ptrs[i]) {
            fprintf(stderr, "malloc(%zu) failed at i=%d\n", alloc_size, i);
            count = i;
            break;
        }

        /* Write to the memory to ensure it's actually allocated */
        memset(ptrs[i], 0xAB, alloc_size);

        printf("  [%d/%d] allocated %zu bytes at %p\n",
               i + 1, count, alloc_size, ptrs[i]);

        usleep(interval_ms * 1000);
    }

    printf("\nAll %d allocations done. Holding for %d seconds...\n",
           count, hold_sec);
    printf("Press Ctrl-C to exit early.\n");

    /* Sleep in chunks so we can respond to signals */
    for (int elapsed = 0; elapsed < hold_sec && !usleep(1000000); elapsed++) {
        if (elapsed % 10 == 9)
            printf("  ... %d/%ds elapsed, %d objects still alive\n",
                   elapsed + 1, hold_sec, count);
    }

    /* Clean up: free all allocations */
    printf("\nFreeing %d allocations...\n", count);
    for (int i = 0; i < count; i++) {
        free(ptrs[i]);
    }
    free(ptrs);

    printf("Done.\n");
    return 0;
}
