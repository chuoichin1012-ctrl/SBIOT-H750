#include "heap_wrap.h"
#include <stdint.h>
#include <string.h>

void *pvPortMalloc(size_t n);
void vPortFree(void *p);

#define HDR 8u  // the block's size, padded so that the data stays 8-byte aligned

void *sb_malloc(size_t n)
{
    if (n > SIZE_MAX - HDR)
        return NULL;
    uint8_t *b = pvPortMalloc(n + HDR);
    if (!b)
        return NULL;
    memcpy(b, &n, sizeof n);
    return b + HDR;
}

void sb_free(void *p)
{
    if (p)
        vPortFree((uint8_t *)p - HDR);
}

size_t sb_size(const void *p)
{
    size_t n = 0;
    if (p)
        memcpy(&n, (const uint8_t *)p - HDR, sizeof n);
    return n;
}

void *sb_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
        return NULL;
    void *p = sb_malloc(count * size);
    if (p)
        memset(p, 0, count * size);
    return p;
}

void *sb_realloc(void *p, size_t n)
{
    if (!p)
        return sb_malloc(n);
    if (!n) {
        sb_free(p);
        return NULL;
    }
    void *q = sb_malloc(n);
    if (!q)
        return NULL;  // the old block stays as it was
    size_t old = sb_size(p);
    memcpy(q, p, old < n ? old : n);
    sb_free(p);
    return q;
}
