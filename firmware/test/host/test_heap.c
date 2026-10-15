// Host tests for the newlib heap wrapper (spec §5.5): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "heap_wrap.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

// heap_5 stand-in: 8-byte aligned blocks, a count of live ones, a failure on demand.
static int s_live, s_fail_next;
static size_t s_last_ask;

void *pvPortMalloc(size_t n)
{
    s_last_ask = n;
    if (s_fail_next) {
        s_fail_next = 0;
        return NULL;
    }
    size_t m = (n + 7) / 8 * 8;
    void *p = aligned_alloc(8, m ? m : 8);
    if (p)
        s_live++;
    return p;
}

void vPortFree(void *p)
{
    if (p) {
        s_live--;
        free(p);
    }
}

int main(void)
{
    char *p = sb_malloc(13);
    CHECK(p && ((uintptr_t)p & 7) == 0 && sb_size(p) == 13 && s_last_ask == 13 + 8, "malloc: 8-aligned, header of 8");
    memset(p, 'a', 13);
    char *q = sb_realloc(p, 100);
    CHECK(q && sb_size(q) == 100 && !memcmp(q, "aaaaaaaaaaaaa", 13) && s_live == 1, "realloc grows, keeps the data, frees the old");
    q = sb_realloc(q, 5);
    CHECK(q && sb_size(q) == 5 && !memcmp(q, "aaaaa", 5) && s_live == 1, "realloc shrinks");
    s_fail_next = 1;
    char *r = sb_realloc(q, 1000);
    CHECK(!r && s_live == 1 && !memcmp(q, "aaaaa", 5) && sb_size(q) == 5, "a failed realloc leaves the old block intact");
    CHECK(sb_realloc(q, 0) == NULL && s_live == 0, "realloc to 0 frees");
    r = sb_realloc(NULL, 7);
    CHECK(r && sb_size(r) == 7 && s_live == 1, "realloc of NULL is malloc");
    sb_free(r);
    sb_free(NULL);
    CHECK(s_live == 0, "free, and free(NULL)");
    unsigned char *z = sb_calloc(10, 7);
    int zero = 1;
    for (int i = 0; z && i < 70; i++)
        zero &= z[i] == 0;
    CHECK(z && zero && sb_size(z) == 70, "calloc zeroes");
    sb_free(z);
    CHECK(sb_calloc((size_t)-1 / 2 + 2, 2) == NULL && s_live == 0, "calloc overflow: NULL, nothing allocated");
    CHECK(sb_malloc((size_t)-1 - 4) == NULL && s_live == 0, "malloc of a size the header would overflow: NULL");
    s_fail_next = 1;
    CHECK(sb_malloc(10) == NULL, "malloc failing: NULL");
    CHECK(sb_size(NULL) == 0, "size of NULL");
    printf("test_heap: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
