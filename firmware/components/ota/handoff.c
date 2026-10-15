#include "handoff.h"
#include <stddef.h>
#include <string.h>
#include "crc32.h"

#define HANDOFF_MAGIC 0x46464F48u  // "HOFF"

void handoff_load(handoff_t *h)
{
    if (h->magic != HANDOFF_MAGIC || h->crc != crc32_update(0, h, offsetof(handoff_t, crc))) {
        memset(h, 0, sizeof *h);
        handoff_store(h);
    }
}

void handoff_store(handoff_t *h)
{
    h->magic = HANDOFF_MAGIC;
    h->crc = crc32_update(0, h, offsetof(handoff_t, crc));
}
