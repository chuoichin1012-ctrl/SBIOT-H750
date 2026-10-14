#include "tick64.h"

uint64_t tick64_update(tick64_t *t, uint32_t raw)
{
    if (raw < t->last)
        t->high++;
    t->last = raw;
    return ((uint64_t)t->high << 32) | raw;
}
