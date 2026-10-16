// cJSON's allocations (spec §5.5, Plan 09c): on the heap, counted. The static arena is gone (Plan 09c ruling 1);
// the count is how the loader tells "out of memory" from a wrong field (Plan 09b).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Installs cJSON's hooks: malloc and free, every NULL counted. Before any config is parsed (app_main). */
void json_alloc_install(void);

/** Allocations through the cJSON hooks that returned NULL, ever. */
unsigned json_alloc_failures(void);

#ifdef __cplusplus
}
#endif
