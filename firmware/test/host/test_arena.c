// Host tests for components/arena: make -C stm32/firmware/test/host
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arena.h"
#include "cJSON.h"
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

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

static _Alignas(8) char region[64 * 1024];

int main(void)
{
    arena_t a;
    arena_init(&a, region, sizeof region);
    json_alloc_install();

    // Outside a scope cJSON uses the heap.
    cJSON *heap = cJSON_CreateString("heap");
    CHECK(heap && !arena_owns(&a, heap), "no scope: heap");

    arena_begin(&a);
    CHECK(arena_current() == &a, "scope is current");
    cJSON *in = cJSON_Parse("{\"k\":[1,2,3],\"s\":\"text\"}");
    CHECK(in && arena_owns(&a, in), "scope: arena");
    size_t live = a.used;
    CHECK(live > 0 && a.peak >= live, "used and peak tracked (%zu, %zu)", live, a.peak);
    cJSON_Delete(in);
    CHECK(a.used == 0, "freed blocks are returned (%zu left)", a.used);
    cJSON_Delete(heap);  // a heap block freed inside a scope still goes back to the heap
    CHECK(arena_malloc(&a, sizeof region) == NULL && a.failed, "an oversized request fails and is flagged");
    arena_end(&a);
    CHECK(arena_current() == NULL, "scope closed");
#if defined(__SANITIZE_ADDRESS__)
    CHECK(__asan_address_is_poisoned(region + 1024), "the region is poisoned after the scope");
#endif

    // Ownership is decided by address range, with no pointer comparison across objects.
    CHECK(!arena_owns(&a, NULL), "NULL is not in the arena");
    CHECK(arena_owns(&a, region) && arena_owns(&a, region + sizeof region - 1), "first and last byte are in the arena");
    CHECK(!arena_owns(&a, region + sizeof region), "one past the end is not in the arena");
    char *elsewhere = malloc(16);
    CHECK(!arena_owns(&a, elsewhere), "a heap block is not in the arena");
    free(elsewhere);

    // A new scope starts from an empty allocator and a fresh peak.
    arena_begin(&a);
    CHECK(a.used == 0 && a.peak == 0 && !a.failed, "fresh scope");
    void *big = arena_malloc(&a, 48 * 1024);
    CHECK(big != NULL, "the whole region is available again");
    arena_end(&a);

    printf(failures ? "%d FAILED\n" : "arena: all tests passed\n", failures);
    return failures != 0;
}
