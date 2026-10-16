#include "hb.h"
#include <stddef.h>
#include <string.h>
#include "crc32.h"

typedef struct {
    char name[HB_NAME_MAX];
    uint32_t timeout_ms;
    uint32_t last_ms;  // atomic
} hb_t;

static hb_t s_hb[HB_MAX];
static int s_n;  // atomic: the slots below it are complete

#define REASON_MAGIC 0x48425253u  // "HBRS"

static int count(void) { return __atomic_load_n(&s_n, __ATOMIC_ACQUIRE); }

int hb_add(const char *name, uint32_t timeout_ms, uint32_t now_ms)
{
    int i = count();
    if (i >= HB_MAX)
        return -1;
    strncpy(s_hb[i].name, name ? name : "?", HB_NAME_MAX - 1);
    s_hb[i].name[HB_NAME_MAX - 1] = '\0';
    s_hb[i].timeout_ms = timeout_ms;
    __atomic_store_n(&s_hb[i].last_ms, now_ms, __ATOMIC_RELAXED);
    __atomic_store_n(&s_n, i + 1, __ATOMIC_RELEASE);  // published once complete
    return i;
}

void hb_beat(int id, uint32_t now_ms)
{
    if (id >= 0 && id < count())
        __atomic_store_n(&s_hb[id].last_ms, now_ms, __ATOMIC_RELAXED);
}

uint32_t hb_late_ms(int id, uint32_t now_ms)
{
    return id >= 0 && id < count() ? now_ms - __atomic_load_n(&s_hb[id].last_ms, __ATOMIC_RELAXED) : 0;
}

int hb_expired(uint32_t now_ms)
{
    int n = count();
    for (int i = 0; i < n; i++)
        if (hb_late_ms(i, now_ms) >= s_hb[i].timeout_ms)
            return i;
    return -1;
}

const char *hb_name(int id)
{
    return id >= 0 && id < count() ? s_hb[id].name : 0;
}

static uint32_t reason_crc(const hb_reason_t *r)
{
    return crc32_update(0, (const uint8_t *)r, offsetof(hb_reason_t, crc));
}

void hb_reason_set(hb_reason_t *r, const char *name, uint32_t late_s)
{
    memset(r, 0, sizeof *r);
    r->magic = REASON_MAGIC;
    strncpy(r->name, name ? name : "?", HB_NAME_MAX - 1);
    r->late_s = late_s;
    r->crc = reason_crc(r);
}

bool hb_reason_get(const hb_reason_t *r, char name[HB_NAME_MAX], uint32_t *late_s)
{
    if (r->magic != REASON_MAGIC || r->crc != reason_crc(r) || memchr(r->name, 0, HB_NAME_MAX) == NULL)
        return false;
    memcpy(name, r->name, HB_NAME_MAX);
    *late_s = r->late_s;
    return true;
}

void hb_reason_clear(hb_reason_t *r) { memset(r, 0, sizeof *r); }

bool hb_restart_was_ours(bool por, bool bor, bool iwdg, bool pin)
{
    return !por && !bor && (iwdg || pin);
}
