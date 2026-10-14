// JSON arena (spec §5.5, §9.3): a fixed region with a TLSF allocator that is
// re-created for every scope, so a request can never fragment it for the next
// one. While a scope is open, cJSON allocates from the arena; at any other time
// it allocates from the heap. Scopes never span a return to an event loop.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *mem;
    size_t size;
    void *tlsf;    // tlsf_t while a scope is open, else NULL
    size_t used;   // bytes held by live allocations, TLSF block overhead included
    size_t peak;   // highest `used` since the scope opened
    bool failed;   // an allocation in this scope returned NULL
} arena_t;

/**
 * Prepares an arena over `mem` (8-byte aligned, larger than TLSF's control block plus pool
 * overhead, about 3.3 KB on the 32-bit target). The region is poisoned until a scope opens.
 */
void arena_init(arena_t *a, void *mem, size_t size);

/** Opens a scope: a fresh allocator over the whole region, and cJSON routed to it. */
void arena_begin(arena_t *a);

/** Closes the scope. Every block from the arena becomes invalid (poisoned under ASan). */
void arena_end(arena_t *a);

void *arena_malloc(arena_t *a, size_t size);
void arena_free(arena_t *a, void *p);
bool arena_owns(const arena_t *a, const void *p);

/** The arena whose scope is open on this task, or NULL. Single-threaded on the host. */
arena_t *arena_current(void);

/** Installs cJSON hooks: blocks come from arena_current() when a scope is open, else from malloc. */
void json_alloc_install(void);

/** Allocations through the cJSON hooks that returned NULL, ever (heap or arena). */
unsigned json_alloc_failures(void);

#ifdef __cplusplus
}
#endif
