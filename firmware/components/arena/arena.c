#include "arena.h"

#include <assert.h>
#include <stdlib.h>
#include "cJSON.h"
#include "tlsf.h"

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define POISON(p, n) ASAN_POISON_MEMORY_REGION(p, n)
#define UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION(p, n)
#else
#define POISON(p, n) ((void)(p), (void)(n))
#define UNPOISON(p, n) ((void)(p), (void)(n))
#endif

// The open scope. Plan 05 makes this per task (a FreeRTOS thread-local pointer).
static arena_t *s_current;
// The one arena whose blocks json_free must recognise, also after its scope closed.
static arena_t *s_known;
static unsigned s_failures;

void arena_init(arena_t *a, void *mem, size_t size)
{
    assert(size > tlsf_size() + tlsf_pool_overhead());  // TLSF keeps its control block inside the region
    *a = (arena_t){.mem = mem, .size = size};
    s_known = a;
    POISON(mem, size);
}

void arena_begin(arena_t *a)
{
    assert(!s_current && !a->tlsf);  // no nesting, one scope at a time
    UNPOISON(a->mem, a->size);
    a->tlsf = tlsf_create_with_pool(a->mem, a->size);
    a->used = a->peak = 0;
    a->failed = false;
    s_current = a;
}

void arena_end(arena_t *a)
{
    assert(s_current == a);
    a->tlsf = NULL;
    s_current = NULL;
    POISON(a->mem, a->size);
}

void *arena_malloc(arena_t *a, size_t size)
{
    void *p = a->tlsf ? tlsf_malloc(a->tlsf, size) : NULL;
    if (!p) {
        a->failed = true;
        return NULL;
    }
    a->used += tlsf_block_size(p) + tlsf_alloc_overhead();
    if (a->used > a->peak) a->peak = a->used;
    return p;
}

void arena_free(arena_t *a, void *p)
{
    if (!p || !a->tlsf) return;  // after the scope closed the whole region is gone anyway
    a->used -= tlsf_block_size(p) + tlsf_alloc_overhead();
    tlsf_free(a->tlsf, p);
}

bool arena_owns(const arena_t *a, const void *p)
{
    const char *c = p, *m = a->mem;
    return c >= m && c < m + a->size;
}

arena_t *arena_current(void) { return s_current; }

static void *json_malloc(size_t size)
{
    arena_t *a = arena_current();
    void *p = a ? arena_malloc(a, size) : malloc(size);
    if (!p) s_failures++;
    return p;
}

unsigned json_alloc_failures(void) { return s_failures; }

static void json_free(void *p)
{
    if (s_known && arena_owns(s_known, p)) arena_free(s_known, p);
    else free(p);
}

void json_alloc_install(void)
{
    cJSON_Hooks h = {.malloc_fn = json_malloc, .free_fn = json_free};
    cJSON_InitHooks(&h);
}
