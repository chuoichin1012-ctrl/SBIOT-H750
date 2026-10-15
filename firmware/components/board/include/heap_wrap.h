// newlib's allocation calls routed to the one heap, FreeRTOS heap_5 in AXI (spec §5.5). An 8-byte
// header before each block keeps its size (realloc needs it) and the 8-byte alignment. Plain C
// over pvPortMalloc/vPortFree, host-tested; app/heap_newlib.c maps malloc, _malloc_r and the
// others onto these.
#pragma once
#include <stddef.h>

void *sb_malloc(size_t n);
void sb_free(void *p);
void *sb_calloc(size_t count, size_t size);  // NULL when count * size overflows
// A new block, the data copied, the old one freed; on failure the old block is left untouched.
void *sb_realloc(void *p, size_t n);
size_t sb_size(const void *p);  // the size asked for (0 for NULL)
