#include "arena.h"

#include <assert.h>
#include <stdint.h>
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

// The open scope, and the task that opened it: only that task allocates from it (Codex 06b r1).
static arena_t *s_current;
static void *s_owner;
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
    s_owner = arena_port_self();
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
    // Compared as integers: relational operators on pointers into different objects are undefined (C11 6.5.8p5).
    uintptr_t c = (uintptr_t)p, m = (uintptr_t)a->mem;
    return p && c >= m && c - m < a->size;
}

// The board's port returns the FreeRTOS task handle (app/arena_port.c). This default makes every
// caller one task: the host tests that need two define their own.
__attribute__((weak)) void *arena_port_self(void) { return NULL; }

arena_t *arena_current(void) { return s_current && s_owner == arena_port_self() ? s_current : NULL; }

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
