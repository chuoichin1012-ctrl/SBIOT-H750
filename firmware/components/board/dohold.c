// The Restart Hold record (dohold.h).
#include "dohold.h"
#include <stddef.h>
#include <string.h>
#include "crc32.h"

#define HOLD_MAGIC 0x44484F44u  // "DOHD"

static uint32_t hold_crc(const do_hold_t *r) { return crc32_update(0, r, offsetof(do_hold_t, crc)); }

void do_hold_set(do_hold_t *r, bool keep, uint8_t latched, uint8_t commanded)
{
    memset(r, 0, sizeof *r);
    r->magic = HOLD_MAGIC;
    r->keep = keep;
    r->latched = latched;
    r->commanded = commanded;
    r->crc = hold_crc(r);
}

bool do_hold_get(const do_hold_t *r, uint8_t *latched, uint8_t *commanded)
{
    if (r->magic != HOLD_MAGIC || r->crc != hold_crc(r) || r->keep > 1)
        return false;
    *latched = r->latched;
    *commanded = r->commanded;
    return true;
}

void do_hold_invalidate(do_hold_t *r) { memset(r, 0, sizeof *r); }

bool do_hold_keep(bool por, bool bor, const do_hold_t *r)
{
    uint8_t l, c;
    return !por && !bor && do_hold_get(r, &l, &c) && r->keep;
}
