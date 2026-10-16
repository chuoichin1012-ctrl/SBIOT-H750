// cJSON's allocations in the host tests, measured (Plan 09c: the static arena is gone, cJSON uses the heap).
// Live bytes and their peak, with an optional limit past which an allocation fails and is counted, as the
// board's allocator counts its failures (json_alloc_failures).
#pragma once
#include <stddef.h>
#include <stdlib.h>
#include "cJSON.h"

static size_t s_meter_live, s_meter_peak, s_meter_limit;
static unsigned s_meter_failures;

static void *meter_malloc(size_t n)
{
    if (s_meter_limit && s_meter_live + n > s_meter_limit) {
        s_meter_failures++;
        return NULL;
    }
    size_t *p = malloc(sizeof(size_t) * 2 + n);  // the size in front, kept 16-aligned
    if (!p) {
        s_meter_failures++;
        return NULL;
    }
    p[0] = n;
    s_meter_live += n;
    if (s_meter_live > s_meter_peak) s_meter_peak = s_meter_live;
    return p + 2;
}

static void meter_free(void *q)
{
    if (!q) return;
    size_t *p = (size_t *)q - 2;
    s_meter_live -= p[0];
    free(p);
}

unsigned json_alloc_failures(void) { return s_meter_failures; }

/** cJSON through the meter; `limit` 0: none. */
static void meter_install(size_t limit)
{
    s_meter_limit = limit;
    cJSON_Hooks h = {meter_malloc, meter_free};
    cJSON_InitHooks(&h);
}

/** Starts a new peak from the bytes live now; returns the peak since the last call. */
static size_t meter_peak_restart(void)
{
    size_t p = s_meter_peak;
    s_meter_peak = s_meter_live;
    return p;
}
