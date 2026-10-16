// The one heap (spec §5.3, §5.5): FreeRTOS heap_5 over 256 KB of AXI SRAM, and newlib's
// allocation calls, reentrant ones included, routed to it through heap_wrap.c. _sbrk always fails
// (components/board/syscalls.c), so nothing allocates outside this heap.
#include <reent.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "heap_wrap.h"
#include "heap_newlib.h"

#define HEAP_SIZE (352u * 1024u)  // 256 KB, plus 96 KB of the AXI the arena and the Edge buffer left (Plan 09c)

static uint8_t s_heap[HEAP_SIZE] __attribute__((section(".axi_bss"), aligned(8)));

void heap_init(void)
{
    static const HeapRegion_t regions[] = {{s_heap, HEAP_SIZE}, {NULL, 0}};
    vPortDefineHeapRegions(regions);
}

size_t heap_free_now(void) { return xPortGetFreeHeapSize(); }
size_t heap_free_min(void) { return xPortGetMinimumEverFreeHeapSize(); }

void *malloc(size_t n) { return sb_malloc(n); }
void free(void *p) { sb_free(p); }
void *calloc(size_t c, size_t n) { return sb_calloc(c, n); }
void *realloc(void *p, size_t n) { return sb_realloc(p, n); }
void *_malloc_r(struct _reent *r, size_t n) { (void)r; return sb_malloc(n); }
void _free_r(struct _reent *r, void *p) { (void)r; sb_free(p); }
void *_calloc_r(struct _reent *r, size_t c, size_t n) { (void)r; return sb_calloc(c, n); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return sb_realloc(p, n); }
