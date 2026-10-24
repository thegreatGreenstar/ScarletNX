/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The process's memory as common.c reads it (snx_footprint, snx_memory_available), which memory_fit takes: told on
   Linux, Android, macOS, iOS and Windows, and the footprint grows with the pages the process touches; neither told
   elsewhere */
#include "common/utilities/common.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const size_t size = (size_t)64 << 20;
    SnxU64 available = 0, before = snx_footprint(), after;
    unsigned char* block = malloc(size);
    /* a page at a time through volatile: a memset of a block freed unread is a store the compiler drops */
    volatile unsigned char* touch = block;
    assert(block);
    for (size_t i = 0; i < size; i += 4096) touch[i] = 1;
    after = snx_footprint();
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    assert(before > 0 && after >= before + size / 2);
    assert(snx_memory_available(&available) && available > 0);
#else
    assert(!before && !after && !snx_memory_available(&available));
#endif
    printf("process memory: footprint %llu MiB, then %llu MiB with %zu MiB touched; %llu MiB available\n",
           (unsigned long long)(before >> 20), (unsigned long long)(after >> 20), size >> 20,
           (unsigned long long)(available >> 20));
    free(block);
    return 0;
}
