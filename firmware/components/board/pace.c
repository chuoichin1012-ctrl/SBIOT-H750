#include "pace.h"

bool pace_due(uint32_t now_ms, uint32_t *last_ms, uint32_t half_ms)
{
    if (now_ms - *last_ms < half_ms)
        return false;
    *last_ms = now_ms;
    return true;
}
