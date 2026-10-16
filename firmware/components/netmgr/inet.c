// The Internet check's verdict per link (Plan 10 ruling 14). See inet.h.
#include "inet.h"

void inet_init(inet_t *s)
{
    s->ok = true;
    s->misses = 0;
}

void inet_result(inet_t *s, bool replied)
{
    if (replied) {
        s->ok = true;
        s->misses = 0;
    } else if (s->misses < INET_MISSES && ++s->misses == INET_MISSES) {
        s->ok = false;
    }
}
