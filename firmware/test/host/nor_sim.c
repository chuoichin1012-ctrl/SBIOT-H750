#include "nor_sim.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rnd(sim_world_t *w)
{
    w->rng = w->rng * 1103515245u + 12345u;
    return w->rng >> 8;
}

void sim_world_init(sim_world_t *w, uint32_t seed)
{
    memset(w, 0, sizeof *w);
    w->cut_at = w->timeout_at = w->hook_at = -1;
    w->rng = seed ? seed : 1;
}

// Counts one operation; returns 1 (power cut: do it partly, then jump), 2 (timeout), or 0.
static int tick(nor_sim_t *s, uint32_t off)
{
    sim_world_t *w = s->w;
    long op = w->ops++;
    if (w->hook && (op == w->hook_at || (w->hook_dev == s->id && off >= w->hook_addr))) {
        void (*h)(sim_world_t *, void *) = w->hook;
        w->hook = 0;  // once
        h(w, w->hook_arg);
    }
    if (op == w->cut_at) {
        if (w->cuts_left > 0) {
            w->cuts_left--;
            w->cut_at = op + w->cut_period;
        }
        return 1;
    }
    if (op == w->timeout_at)
        return 2;
    for (int i = 0; i < 2; i++) {
        int c = w->fail[i].code;
        if (w->fail[i].dev == s->id && off >= w->fail[i].lo && off < w->fail[i].hi && c != SIM_READ_EIO &&
            c != SIM_READ_TIMEOUT)
            return c == NOR_ETIMEOUT ? 2 : c == SIM_SILENT ? 4 : c == SIM_ERASE_SILENT ? 5 : 3;
    }
    return 0;
}

static int sim_read(nor_t *n, uint32_t off, void *buf, uint32_t len)
{
    nor_sim_t *s = (nor_sim_t *)n;
    if (off > s->size || len > s->size - off)
        return NOR_EIO;
    for (int i = 0; i < 2; i++) {
        int c = s->w->fail[i].code;
        if (s->w->fail[i].dev == s->id && off < s->w->fail[i].hi && off + len > s->w->fail[i].lo &&
            (c == SIM_READ_EIO || c == SIM_READ_TIMEOUT)) {
            if (s->w->fail[i].count > 0 && --s->w->fail[i].count == 0)
                s->w->fail[i].dev = 0;
            return c == SIM_READ_TIMEOUT ? NOR_ETIMEOUT : NOR_EIO;
        }
    }
    memcpy(buf, s->mem + off, len);
    return NOR_OK;
}

static int sim_erase(nor_t *n, uint32_t off)
{
    nor_sim_t *s = (nor_sim_t *)n;
    if (off % NOR_SECTOR || off >= s->size)
        return NOR_EIO;
    int t = tick(s, off);
    if (t == 2)
        return NOR_ETIMEOUT;
    if (t == 3)
        return NOR_EIO;
    if (t == 4)
        t = 0;  // SIM_SILENT only affects programming
    if (t == 5)
        return NOR_OK;  // SIM_ERASE_SILENT: reported done, nothing erased
    if (t == 1) {
        uint32_t k = rnd(s->w) % NOR_SECTOR;
        memset(s->mem + off, 0xFF, k);
        s->mem[off + k] |= (uint8_t)rnd(s->w);  // half erased
        longjmp(*s->w->cut_jmp, 1);
    }
    memset(s->mem + off, 0xFF, NOR_SECTOR);
    return NOR_OK;
}

static int sim_program(nor_t *n, uint32_t off, const void *buf, uint32_t len)
{
    nor_sim_t *s = (nor_sim_t *)n;
    if (len == 0 || len > NOR_PAGE || off / NOR_PAGE != (off + len - 1) / NOR_PAGE || off + len > s->size)
        return NOR_EIO;
    int t = tick(s, off);
    if (t == 2)
        return NOR_ETIMEOUT;
    if (t == 3)
        return NOR_EIO;
    if (t == 4)
        return NOR_OK;  // silent: nothing programmed
    if (t == 5)
        t = 0;  // SIM_ERASE_SILENT only affects erases
    const uint8_t *p = buf;
    uint32_t done = t == 1 ? rnd(s->w) % len : len;
    for (uint32_t i = 0; i < done; i++)
        s->mem[off + i] &= p[i];
    if (t == 1) {
        s->mem[off + done] &= (uint8_t)(p[done] | rnd(s->w));  // half programmed
        longjmp(*s->w->cut_jmp, 1);
    }
    return NOR_OK;
}

void nor_sim_init(nor_sim_t *s, sim_world_t *w, int id, uint32_t size)
{
    s->nor.read = sim_read;
    s->nor.erase_sector = sim_erase;
    s->nor.program = sim_program;
    s->nor.ctx = s;
    s->mem = malloc(size);
    memset(s->mem, 0xFF, size);
    s->size = size;
    s->id = id;
    s->w = w;
}

void sim_fail(sim_world_t *w, int i, int dev, uint32_t lo, uint32_t hi, int code)
{
    w->fail[i].dev = dev;
    w->fail[i].lo = lo;
    w->fail[i].hi = hi;
    w->fail[i].code = code;
    w->fail[i].count = 0;
}

void nor_sim_free(nor_sim_t *s)
{
    free(s->mem);
}
