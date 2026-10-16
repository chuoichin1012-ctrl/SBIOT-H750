// The app's heap (spec §5.5): heap_init() before any allocation, in app_main.
#pragma once
#include <stddef.h>

void heap_init(void);
size_t heap_free_now(void);
size_t heap_free_min(void);  // the lowest free size since the boot
size_t heap_largest_free(void);  // the largest free block now (fragmentation, Plan 09c)
