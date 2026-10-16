#include "arena.h"

#include <stdlib.h>
#include "cJSON.h"

static unsigned s_failures;

static void *json_malloc(size_t size)
{
    void *p = malloc(size);
    if (!p) s_failures++;
    return p;
}

unsigned json_alloc_failures(void) { return s_failures; }

void json_alloc_install(void)
{
    cJSON_Hooks h = {.malloc_fn = json_malloc, .free_fn = free};
    cJSON_InitHooks(&h);
}
